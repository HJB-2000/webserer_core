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
                return;  // wait for EPOLLOUT to drain the error response
            }
            
            if (conn->request().parse_state == PSTATE_COMPLETE)
            {
                conn->setProcessing();
                CgiRequestInfo cgi;
                if (_responder.resolveCgiRequest(conn->request(), *conn->config(), cgi))
                {
                    _ApiStartCgi(conn, cgi);
                    // extern void enforce_memory_limit();
                    // enforce_memory_limit();
                    return;
                }

                _responder.handle(conn->request(),
                                *conn->config(),
                                conn->writeBuffer());

                conn->setWriting();
                _rearmClient(fd);
                return;
            }
            // PS_IDLE / PS_HEADERS / PS_BODY → partial, keep reading
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

// void EventLoop::_handleWrite(Connection* conn)
// {
//     const int fd = conn->fd();

//     while (!conn->writeBuffer().empty())
//     {
//         ssize_t n = conn->send();
//         if (n < 0)
//         {
//             if (errno == EAGAIN || errno == EWOULDBLOCK)
//                 break;  // kernel buffer full — wait for next EPOLLOUT
//             std::cerr << "[EventLoop] send error on fd " << fd
//                       << ": " << std::strerror(errno) << "\n";
//             _closeClient(fd);
//             return;
//         }
//     }

//     if (conn->writeBuffer().empty())
//     {
//         if (!conn->peerHalfClosed() && conn->request().keepAlive())
//         {
//             conn->setReading(); // resets buffers + request + stamps time
//             _rearmClient(fd);  // re-arm EPOLLIN
//         }
//         else
//         {
//             _closeClient(fd);
//         }
//     }
//     // else: buffer not empty — EPOLLOUT will fire again
// }

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
 
    // std::cerr << "[WR-DBG] fd=" << fd
    //           << " wbuf_after_send=" << conn->writeBuffer().size()
    //           << " cgi_jobs=" << _cgi_jobs.size() << "\n";
 
    // ── check if a CGI job is still streaming for this client ──
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
        // CGI still running — never transition to READING yet.
        if (conn->writeBuffer().size() < CGI_STREAM_LWM)
        {
            // std::cerr << "[WR-DBG] releasing bp: cgi_fd=" << active_cgi_fd
            //           << " client_fd=" << fd
            //           << " wbuf=" << conn->writeBuffer().size() << "\n";
            _modifyEventFd(active_cgi_fd, EV_CGI,
                           EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
            _handleCgiEvent(active_cgi_fd, EPOLLIN);
            if (!_manager->get(fd))
                return;
        }
 
        // After draining / after CGI added more data:
        // always keep client armed for EPOLLOUT so we keep draining.
        // setWriting() is idempotent — safe to call even if already WRITING.
        conn->setWriting();
        _rearmClient(fd);
 
        // std::cerr << "[WR-DBG] cgi active: fd=" << fd
        //           << " kept EPOLLOUT wbuf=" << conn->writeBuffer().size() << "\n";
        return;
    }
 
    // No active CGI — normal finish path.
    if (!_manager->get(fd))
        return;
 
    if (conn->writeBuffer().empty())
    {
        if (!conn->peerHalfClosed() && conn->request().keepAlive())
        {
            // std::cerr << "[WR-DBG] fd=" << fd << " -> setReading\n";
            conn->setReading();
            _rearmClient(fd);
        }
        else
        {
            // std::cerr << "[WR-DBG] fd=" << fd << " -> closeClient\n";
            _closeClient(fd);
        }
    }
    else
    {
        // std::cerr << "[WR-DBG] fd=" << fd << " -> rearm EPOLLOUT wbuf="
                //   << conn->writeBuffer().size() << "\n";
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
    // EPOLLERR/EPOLLHUP on the write end means the child closed its stdin
    // (or died). Close our end; the child either has what it needs or is gone.
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
