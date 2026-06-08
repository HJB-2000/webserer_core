#include "Headers/EventLoop.hpp"
#include "cgi/CgiHandler.hpp"


void EventLoop::_addCgiFd(int result_fd, int client_fd)
{
    try {
        _registerEventFd(result_fd, EV_CGI, EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
        std::cerr << "[EventLoop] CGI fd " << result_fd
                  << " registered for client fd " << client_fd << "\n";
    }
    catch (const std::exception& e)  
    {  
        std::cerr << "[EventLoop] failed to register CGI fd " << result_fd   
                  << ": " << e.what() << "\n";  
        ::close(result_fd);  
        throw;  
    }  

}

void EventLoop::_ApiStartCgi(Connection* conn, const CgiRequestInfo& info)
{
    int fds[2];
    if (::pipe(fds) < 0)
    {
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }
    if (::fcntl(fds[0], F_SETFD, FD_CLOEXEC) < 0 || 
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC) < 0)
    {
        ::close(fds[0]);
        ::close(fds[1]);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }

    int result_read_fd = fds[0];
    int result_write_fd = fds[1];

    if (EventLoop::setNonBlocking(result_read_fd) < 0)
    {
        ::close(result_read_fd);
        ::close(result_write_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }

    CgiJob* job = NULL;
    try {
        job = new CgiJob(conn->fd(), result_read_fd, conn->writeBuffer().maxSize(), conn->config()->get_timeout_seconds());
        _addCgiFd(result_read_fd, conn->fd());
        _cgi_jobs[result_read_fd] = job;
    }
    catch (const std::exception& ex)
    {
        if (job != NULL) {
            delete job; 
            job = NULL;
        }
        throw;
    }
    conn->setCgiRunning();
    _rearmClient(conn->fd());

    CgiHandler cgi(conn->request(), *conn->config(), *info.location, info.script_path, conn->get_clientIp());
    
    bool ok = cgi.startCgi(result_write_fd);
    if (ok)
    {
        job->child_pid = cgi.getChildPid();
        int stdin_fd = cgi.releaseStdinFd();
        if (stdin_fd >= 0)
        {
            _setCloexec(stdin_fd, "cgi-stdin");
            job->stdin_fd     = stdin_fd;
            job->stdin_body.swap(conn->request().body);
            job->stdin_offset = 0;
            _cgi_stdin_jobs[stdin_fd] = job;
            try {
                _registerEventFd(stdin_fd, EV_CGI_STDIN,
                                 EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP);
            }
            catch (const std::exception& ex) {
                std::cerr << "[EventLoop] failed to register CGI stdin fd " << stdin_fd   
                  << ": " << ex.what() << "\n";  
                _closeCgiStdin(job); 
            }
        }
    }
    ::close(result_write_fd);

    if (!ok)
    {
        _closeCgiJob(result_read_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }
}

void EventLoop::_handleCgiEvent(int result_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    CgiJob* job = it->second;

    if (events & (EPOLLERR | EPOLLHUP))
    {
        // still try to drain
    }

    char buf[8192];
    try
    {
        while (true)
        {
            ssize_t n = ::read(result_fd, buf, sizeof(buf));
            if (n > 0)
            {
                job->result_buffer.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
            {
                _finishCgiJob(result_fd);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;

            _failCgiJob(result_fd, 502);
            return;
        }
    }
    catch (const BodyLimitException&)
    {
        _failCgiJob(result_fd, 413);
    }
}
