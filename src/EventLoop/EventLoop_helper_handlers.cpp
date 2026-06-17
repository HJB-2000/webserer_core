#include "Headers/EventLoop.hpp"
#include <string>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <cerrno>
#include <cstring>

void EventLoop::_handleAccept(int server_fd)
{
    const ServerConfig* config = _configForServer(server_fd);

    while (true)
    {
        int client_fd = _manager->addConnection(server_fd, config);
        if (client_fd < 0)
            break;
        _setCloexec(client_fd, "client");
        try {
            _registerEventFd(client_fd, EV_CLIENT, EPOLLIN | EPOLLET | EPOLLRDHUP);
        }
        catch(const std::exception& ex) {
           std::cerr << "[EventLoop] failed to register client fd " << client_fd   
                      << ": " << ex.what() << "\n";  
            _manager->closeConnection(client_fd);  
            continue; 
        }
    }
}

void EventLoop::_handleError(Connection* conn)
{
    const int fd = conn->fd();
    std::cerr << "[EventLoop] error/hup on fd " << fd << "\n";
    _closeClient(fd);
}


std::string int_to_string(int number) {
    std::ostringstream oss;
    oss << number;
    return oss.str();
}

static bool pathRequiresCgiBodySpool(const HttpRequest& req,
                                     const ServerConfig& cfg)
{
    const Location* loc = cfg.matchLocation(req.path);
    if (!loc || loc->getCGI_map().empty())
    { 
        return false; 
    }

    const std::map<std::string, std::string>& exts = loc->getCGI_map();
    for (std::map<std::string, std::string>::const_iterator it = exts.begin(); it != exts.end(); ++it)
    {
        const std::string& ext = it->first;
        if (req.path.size() >= ext.size()
            && req.path.compare(req.path.size() - ext.size(), ext.size(), ext) == 0)
            return true;
    }
    return false;
}

static bool isBodyFullySpooled(Connection* conn)
{
    const HttpRequest& req = conn->request();

    if (!req.expectsBody())
        return true;

    if (pathRequiresCgiBodySpool(req, *conn->config()))
    {
        if (!req.body.empty())
            return false;
        if (req.parse_state != PSTATE_COMPLETE)
            return false;
        if (req.body_file_written == 0 || req.body_file_written != req.written)
            return false;
        if (req.content_length > 0 && req.body_file_written < req.content_length)
            return false;
        return true;
    }

    if (req.chunked)
        return req.parse_state == PSTATE_COMPLETE;
    if (req.content_length > 0)
        return req.written >= req.content_length;
    if (req.method == "POST")
        return req.parse_state == PSTATE_COMPLETE && req.written > 0;
    return true;
}

static void cleanupBodyTmpFile(HttpRequest& req)
{
    if (req.opened_file >= 0)
    {
        ::close(req.opened_file);
        req.opened_file = -1;
    }
    if (!req.tmp_body_path.empty())
    {
        std::remove(req.tmp_body_path.c_str());
        req.tmp_body_path.clear();
    }
    req.opened = false;
}

static void finalizeBodyTmpFile(Connection* conn)
{
    HttpRequest& req = conn->request();

    if (!req.opened || req.opened_file < 0 || req.body_file_written == 0)
        return;

    ::close(req.opened_file);
    req.opened_file = ::open(req.tmp_body_path.c_str(), O_RDONLY);
    if (req.opened_file < 0)
    {
        conn->request().parse_state = PSTATE_ERROR;
        conn->request().error_code = 500;
    }
}

static void resumeBodyIfNeeded(Connection* conn)
{
    HttpRequest& req = conn->request();

    if (req.parse_state != PSTATE_COMPLETE)
        return;

    if (req.content_length > 0 && req.body_file_written < req.content_length)
    {
        req.parse_state = PSTATE_BODY;
        return;
    }

    if (!req.chunked && req.content_length == 0
        && (req.method == "POST")
        && !conn->readBuffer().empty())
    {
        req.parse_state = PSTATE_BODY;
    }
}

static void maybeCompletePostWithoutLength(Connection* conn)
{
    HttpRequest& req = conn->request();

    if (req.parse_state != PSTATE_BODY || req.chunked || req.content_length > 0)
        return;
    if (req.method != "POST")
        return;
    if (req.written == 0 || !conn->readBuffer().empty())
        return;

    req.parse_state = PSTATE_COMPLETE;
}

static void flushRequestBodyToTmpFile(Connection* conn)
{
    HttpRequest& req = conn->request();

    if (req.body.empty())
        return;

    if (req.max_body_size > 0
        && req.body_file_written + req.body.size() > req.max_body_size)
    {
        req.parse_state = PSTATE_ERROR;
        req.error_code  = 413;
        return;
    }
    static unsigned long counter = 0;
    if (!req.opened)
    {
        counter++;
        std::ostringstream name;
        name << "/tmp/webserv_" << counter
             << "_" << conn->conn_num
             << "_" << static_cast<long>(std::time(NULL));
        req.tmp_body_path = name.str();
        req.opened_file = ::open(req.tmp_body_path.c_str(),
                                 O_RDWR | O_CREAT | O_TRUNC | O_EXCL, 0600);
        if (req.opened_file < 0)
        {
            std::cerr << "[EventLoop] tmp file open failed: "
                      << std::strerror(errno) << "\n";
            req.tmp_body_path.clear();
            req.parse_state = PSTATE_ERROR;
            req.error_code  = 503;
        }
        req.opened = true;
    }

    size_t offset = 0;
    while (offset < req.body.size())
    {
        ssize_t n = ::write(req.opened_file,
                            req.body.data() + offset,
                            req.body.size() - offset);
        if (n < 0)
        {
            std::cerr << "[EventLoop] tmp file write failed: "
                      << std::strerror(errno) << "\n";
            req.parse_state = PSTATE_ERROR;
            req.error_code  = 503;
            return;
        }
        if (n == 0)
            break;
        offset += static_cast<size_t>(n);
    }
    req.body_file_written += offset;
    req.body.reset();
}


bool EventLoop::_tryDispatchComplete(Connection* conn)
{
    const int fd = conn->fd();

    if (conn->request().parse_state != PSTATE_COMPLETE)
        return false;
    if (!isBodyFullySpooled(conn))
        return false;

    conn->setProcessing();
    if (pathRequiresCgiBodySpool(conn->request(), *conn->config()))
        finalizeBodyTmpFile(conn);

    CgiRequestInfo cgi;
    if (_responder.resolveCgiRequest(conn->request(), *conn->config(), cgi))
    {
        if (conn->request().body_file_written > 0
            && conn->request().opened_file < 0)
        {
            _responder.sendError(conn->request().error_code, *conn->config(), conn->writeBuffer());
            conn->setWriting();
            _rearmClient(fd);
            return true;
        }
        _ApiStartCgi(conn, cgi);
        conn->request().opened = false;
        if(conn->request().opened_file > 0)
        {
            ::close(conn->request().opened_file);
            conn->request().opened_file = -1; 
        }
        return true;
    }

    cleanupBodyTmpFile(conn->request());

    _responder.handle(conn->request(), *conn->config(), conn->writeBuffer());
    conn->setWriting();
    _rearmClient(fd);
    return true;
}

void EventLoop::_handleRead(Connection* conn)
{
    const int fd = conn->fd();

    if (conn->state() == CSTATE_CGI_RUNNING)
        return;

    try
    {
        while (true)
        {
            ssize_t n = conn->recv();

            if (n == 0)
            {
                HttpRequest& req = conn->request();
                if (req.parse_state == PSTATE_BODY && !req.chunked
                    && req.content_length == 0 && req.written > 0
                    && pathRequiresCgiBodySpool(req, *conn->config()))
                {
                    flushRequestBodyToTmpFile(conn);
                    req.parse_state = PSTATE_COMPLETE;
                    if (_tryDispatchComplete(conn))
                        return;
                }
                _closeClient(fd);
                return;
            }
            if (n < 0)
            {
                maybeCompletePostWithoutLength(conn);
                if (pathRequiresCgiBodySpool(conn->request(), *conn->config()))
                    flushRequestBodyToTmpFile(conn);
                break;
            }

            resumeBodyIfNeeded(conn);
            _parser.feed(conn);

            if (conn->request().parse_state == PSTATE_ERROR)
            {
                std::cerr << "[EventLoop] parse error " << conn->request().error_code
                          << " on fd " << fd << "\n";
                _responder.sendError(conn->request().error_code,
                                     *conn->config(),
                                     conn->writeBuffer());
                conn->setWriting();
                _rearmClient(fd);
                return;
            }

            if ((conn->request().parse_state == PSTATE_BODY ||
                 conn->request().parse_state == PSTATE_COMPLETE)
                && pathRequiresCgiBodySpool(conn->request(), *conn->config()))
            {
                flushRequestBodyToTmpFile(conn);
            }
            
            maybeCompletePostWithoutLength(conn);

            if (_tryDispatchComplete(conn))
                return;
        }

        if (_tryDispatchComplete(conn))
            return;
    }
    catch (const BodyLimitException&)
    {
        std::cerr << "[EventLoop] body limit exceeded on fd " << fd << "\n";
        conn->request().headers["connection"] = "close";
        _responder.sendError(413, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(fd);
    }
}

void EventLoop::_handleWrite(Connection* conn)
{
    const int fd = conn->fd();
 
    while (!conn->writeBuffer().empty())
    {
        ssize_t n = conn->send();
        if (n < 0)
        {
            break;
        }
    }

    static const size_t CGI_STREAM_LWM = 64 * 1024;
 
    bool has_active_cgi = false;
    int  active_cgi_fd  = -1;
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        if (it->second->client_fd == fd)
        {
            has_active_cgi = true;
            active_cgi_fd  = it->first;
            break;
        }
    }
 
    if (has_active_cgi)
    {
        if (conn->writeBuffer().size() < CGI_STREAM_LWM)
        {
            _modifyEventFd(active_cgi_fd, EV_CGI,
                           EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
            _handleCgiEvent(active_cgi_fd, EPOLLIN);
            if (!_manager->get(fd))
                return;
        }
 
        conn->setWriting();
        _rearmClient(fd);
 
        return;
    }
 
    if (!_manager->get(fd))
        return;
 
    if (conn->writeBuffer().empty())
    {
        if (!conn->peerHalfClosed() && conn->request().keepAlive())
        {
            conn->setReading();
            _rearmClient(fd);
        }
        else
        {
            _closeClient(fd);
        }
    }
    else
    {
        conn->setWriting();
        _rearmClient(fd);
    }
}
 

void EventLoop::_handleClientEvent(int client_fd, uint32_t events)
{
    Connection* conn = _manager->get(client_fd);
    if (!conn)
    {
        std::cerr << "[EventLoop] stale event — connection already gone\n";
        return;
    }

    if (events & (EPOLLERR | EPOLLHUP))
    {
        _handleError(conn);
        return;
    }

    if (events & EPOLLRDHUP)
    {
        bool has_active_cgi = false;
        for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
            it != _cgi_jobs.end(); ++it)
        {
            if (it->second->client_fd == client_fd) 
            { 
                has_active_cgi = true; 
                break; 
            }
        }
        if (conn->writeBuffer().empty() || has_active_cgi)
        {
            _closeClient(client_fd);
        }
        else
            conn->setPeerHalfClosed();

        return;
    }


    if (events & EPOLLIN)
    {
        _handleRead(conn);
        return;
    }

    if (events & EPOLLOUT)
    {
        _handleWrite(conn);
        return;
    }
}
