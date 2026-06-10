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


void EventLoop::_handleRead(Connection* conn)
{
    const int fd = conn->fd();

    try
    {
        while (true)
        {
            ssize_t n = conn->recv();

            if (n == 0)
            {
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
            
            if (conn->request().parse_state == PSTATE_COMPLETE)
            {
                conn->setProcessing();
                CgiRequestInfo cgi;
                if (_responder.resolveCgiRequest(conn->request(), *conn->config(), cgi))
                {
                    _ApiStartCgi(conn, cgi);
                    return;
                }

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
        if (conn->writeBuffer().empty())
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
    _closeCgiStdin(job);
}
