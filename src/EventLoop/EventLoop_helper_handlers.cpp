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
                    break;  // edge-trigger drained
                std::cerr << "[EventLoop] recv error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _closeClient(fd);
                return;
            }

            // ── Phase 2: HttpParser ──────────────────────────
            // _parser.feed(conn->readBuffer(), conn->request());
            _parser.feed(conn);
            // ─────────────────────────────────────────────────

            if (conn->request().parse_state == PSTATE_ERROR)
            {

                // conn->request().headers["connection"] = "close";
                std::cerr << "[EventLoop] parse error " << conn->request().error_code
                          << " on fd " << fd << "\n";
                // Phase 3: real error response
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
                    _startCgi(conn, cgi);
                    return;
                }

                _responder.handle(conn->request(),
                                *conn->config(),
                                conn->writeBuffer());

                conn->setWriting();
                _rearmClient(fd);
                return;
            }
            break;
            // PS_IDLE / PS_HEADERS / PS_BODY → partial, keep reading
        }
    }
    catch (const BodyLimitException&)
    {
        // readBuffer exceeded client_max_body_size → 413
        conn->request().headers["connection"] = "close";
        // Phase 3: real 413 response
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
                break;  // kernel buffer full — wait for next EPOLLOUT
            std::cerr << "[EventLoop] send error on fd " << fd
                      << ": " << std::strerror(errno) << "\n";
            _closeClient(fd);
            return;
        }
    }

    if (conn->writeBuffer().empty())
    {
        if (!conn->peerHalfClosed() && conn->request().keepAlive())
        {
            conn->setReading();        // resets buffers + request + stamps time
            _rearmClient(fd);  // re-arm EPOLLIN
        }
        else
        {
            _closeClient(fd);
        }
    }
    // else: buffer not empty — EPOLLOUT will fire again
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

    // EPOLLERR/EPOLLHUP on the write end means the child closed its stdin
    // (or died). Close our end; the child either has what it needs or is gone.
    if (events & (EPOLLERR | EPOLLHUP))
    {
        _closeCgiStdin(job);
        return;
    }

    while (job->stdin_offset < job->stdin_body.size())
    {
        const char*  data = job->stdin_body.data() + job->stdin_offset;
        const size_t left = job->stdin_body.size() - job->stdin_offset;
        ssize_t n = ::write(stdin_fd, data, left);
        if (n > 0)
        {
            job->stdin_offset += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;  // pipe full — wait for next EPOLLOUT

        // EPIPE or other error: child closed stdin or died. Give up writing,
        // but keep reading its stdout — it may still have produced output.
        _closeCgiStdin(job);
        return;
    }

    // Body fully written — close write end to signal EOF to the child.
    _closeCgiStdin(job);
}