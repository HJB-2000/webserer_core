#include "Headers/EventLoop.hpp"

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


void EventLoop::_tryFlushToCgiStdin(CgiJob* job, Connection* conn)
{
    if (!job || job->stdin_fd < 0)
        return;

    Buffer& body = conn->request().body;
    while (body.size() > 0)
    {
        const char*  data = body.data();
        const size_t left = body.size();

        ssize_t n = ::write(job->stdin_fd, data, left);

        if (n > 0)
        {
            body.consume(static_cast<size_t>(n));
            job->body_written += static_cast<size_t>(n);
            continue;
        }

        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // pipe full — register EV_CGI_STDIN if not already registered
                if (_cgi_stdin_jobs.find(job->stdin_fd) == _cgi_stdin_jobs.end())
                {
                    _cgi_stdin_jobs[job->stdin_fd] = job;
                    try {
                        _registerEventFd(job->stdin_fd, EV_CGI_STDIN,
                                         EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP);
                    }
                    catch (const std::exception& ex)
                    {
                        std::cerr << "[EventLoop] failed to register CGI stdin fd "
                                  << job->stdin_fd << ": " << ex.what() << "\n";
                        _closeCgiStdin(job);
                    }
                }
                return;
            }
            // write error: close stdin
            _closeCgiStdin(job);
            return;
        }
    }

    // body fully flushed
    // if parse is complete and body is done, close stdin
    if (conn->request().parse_state == PSTATE_COMPLETE)
    {
        _closeCgiStdin(job);
    }
}


void EventLoop::_handleRead(Connection* conn)
{
    const int fd = conn->fd();

    try
    {
        while (true)
        {
            // Check HWM BEFORE reading more data - prevents unbounded body growth
            if (conn->state() == CSTATE_CGI_RUNNING)
            {
                static const size_t CGI_BODY_HWM = 256 * 1024;
                if (conn->request().body.size() >= CGI_BODY_HWM)
                    break;  // Stop reading, let _handleCgiStdinEvent drain first
            }

            ssize_t n = conn->recv();

            if (n == 0)
            {
                // If CGI is running, the client just closed its write side.
                // Don't kill the CGI - wait for it to produce output.
                if (conn->state() == CSTATE_CGI_RUNNING)
                {
                    conn->setPeerHalfClosed();
                    break;
                }
                _closeClient(fd);
                return;
            }

            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                std::cerr << "[EventLoop] recv error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _closeClient(fd);
                return;
            }

            _parser.feed(conn);

            ParseState ps = conn->request().parse_state;

            if (ps == PSTATE_ERROR)
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

            // Fork CGI as soon as we know it's a CGI request,
            // whether headers-only (GET) or headers+partial body (POST)
            if ((ps == PSTATE_HEADERS_DONE || ps == PSTATE_COMPLETE)
                && conn->state() != CSTATE_CGI_RUNNING)
            {
                conn->setProcessing();
                CgiRequestInfo cgi;
                if (_responder.resolveCgiRequest(conn->request(), *conn->config(), cgi))
                {
                    _ApiStartCgi(conn, cgi);
                    // fall through — don't return
                }
            }

            if (conn->state() == CSTATE_CGI_RUNNING)
            {
                // pump whatever body bytes the parser just decoded into stdin
                CgiJob* job = NULL;
                for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
                     it != _cgi_jobs.end(); ++it)
                {
                    if (it->second->client_fd == fd)
                    {
                        job = it->second;
                        break;
                    }
                }
                _tryFlushToCgiStdin(job, conn);
                // Only close stdin when body is empty AND parse is complete.
                // If body not empty, _handleCgiStdinEvent will flush remaining data.
                if (ps == PSTATE_COMPLETE && job && conn->request().body.size() == 0)
                    _closeCgiStdin(job);   // signal EOF to child

                // HWM check is at the top of the while loop - continue to flush
                continue;
            }

            if (ps == PSTATE_COMPLETE)
            {
                // non-CGI path
                _responder.handle(conn->request(),
                                *conn->config(),
                                conn->writeBuffer());

                conn->setWriting();
                _rearmClient(fd);
                return;
            }
        }
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
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            std::cerr << "[EventLoop] send error on fd " << fd
                      << ": " << std::strerror(errno) << "\n";
            _closeClient(fd);
            return;
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
        // Don't close if CGI is running — we need to wait for CGI output
        if (conn->writeBuffer().empty() && conn->state() != CSTATE_CGI_RUNNING)
            _closeClient(client_fd);
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

void EventLoop::_handleCgiStdinEvent(int stdin_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_stdin_jobs.find(stdin_fd);
    if (it == _cgi_stdin_jobs.end())
        return;

    CgiJob* job = it->second;
    Connection* conn = _manager->get(job->client_fd);
    if (!conn)
    {
        _closeCgiStdin(job);
        return;
    }
    Buffer& body = conn->request().body;
    if (events & (EPOLLERR | EPOLLHUP))
    {
        _closeCgiStdin(job);
        return;
    }

    while (body.size() > 0)
    {
        const char*  data = body.data();
        const size_t left = body.size();

        ssize_t n = ::write(stdin_fd, data, left);

        if (n > 0)
        {
            body.consume(static_cast<size_t>(n));
            job->body_written += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;

        _closeCgiStdin(job);
        return;
    }
    // body temporarily empty — more data may still be in flight.
    // Only close stdin when body is empty AND parse is complete.
    if (conn->request().parse_state == PSTATE_COMPLETE)
        _closeCgiStdin(job);

    // Body low-water-mark: once body drains below LWM, resume socket
    // draining so we don't deadlock when CGI is slow but still consuming.
    static const size_t CGI_BODY_LWM = 64 * 1024;
    if (body.size() < CGI_BODY_LWM)
        _handleRead(conn);
}
