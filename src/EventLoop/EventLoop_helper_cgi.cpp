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

void EventLoop::_startCgi(Connection* conn, const CgiRequestInfo& info)
{
    int fds[2];
    if (::pipe(fds) < 0)
    {
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }

    // apply the FD_CLOEXEC flag to both file descriptors via fcntl
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
    conn->readBuffer().reset();
    _rearmClient(conn->fd());

    // bool ok = startCgi(conn->request(), *conn->config(), *info.location,
    //                    info.script_path, result_write_fd);
    CgiHandler cgi(conn->request(), *conn->config(), *info.location, info.script_path, conn->get_clientIp()); // sending the ip_client for the meta-variables.
    bool ok = cgi.startCgi(result_write_fd);
    if (ok)
    {
        job->child_pid = cgi.getChildPid();

        // If the request has a body, take ownership of the (non-blocking)
        // stdin write fd and schedule writes via EPOLLOUT. This avoids the
        // pipe-buffer deadlock for bodies larger than ~64KB.
        int stdin_fd = cgi.releaseStdinFd();
        if (stdin_fd >= 0)
        {
            _setCloexec(stdin_fd, "cgi-stdin");
            job->stdin_fd     = stdin_fd;
            job->stdin_body   = &conn->request().body;
            job->stdin_offset = 0;
            _cgi_stdin_jobs[stdin_fd] = job;
            try {
                _registerEventFd(stdin_fd, EV_CGI_STDIN,
                                 EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP);
            }
            /* this is a problem cause without the stdin we are gonna recieve the body*/
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
        // FIX: Free the request body to prevent memory leak when CGI fails to start
        std::string().swap(const_cast<HttpRequest&>(conn->request()).body);
        conn->readBuffer().reset();
        if (conn->request().body_fd >= 0)
        {
            ::close(conn->request().body_fd);
            const_cast<HttpRequest&>(conn->request()).body_fd = -1;
        }
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }
}

void EventLoop::_closeCgiStdin(CgiJob* job)
{
    if (!job || job->stdin_fd < 0)
        return;

    int fd = job->stdin_fd;
    _cgi_stdin_jobs.erase(fd);
    _unregisterEventFd(fd);
    ::close(fd);

    job->stdin_fd     = -1;
    job->stdin_offset = 0;
    job->stdin_body = NULL;
}

// Wrapped the read loop in a try/catch block to return a clean 413 instead of crashing.
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
                if (job->result_buffer.size() + static_cast<size_t>(n) > job->result_buffer.maxSize())
                    throw BodyLimitException("CGI response too large");
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

void EventLoop::_finishCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end())
        return;
    CgiJob* job = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.handleCgiOutput(conn->request(),
                                   *conn->config(),
                                   job->result_buffer,
                                   conn->writeBuffer());
        conn->setWriting();
        // FIX: Free the request body to prevent memory leak
        std::string().swap(const_cast<HttpRequest&>(conn->request()).body);
        conn->readBuffer().reset();
        // Close body_fd if it was opened for large POST bodies
        if (conn->request().body_fd >= 0)
        {
            ::close(conn->request().body_fd);
            const_cast<HttpRequest&>(conn->request()).body_fd = -1;
        }
        _rearmClient(conn->fd());
    }

    // FIX: Reset result_buffer to free memory before deleting the job
    job->result_buffer.reset();
    _closeCgiJob(result_fd);
}

void EventLoop::_failCgiJob(int result_fd, int status_code)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end())
        return;
    CgiJob* job = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.sendError(status_code, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        // FIX: Free the request body to prevent memory leak
        std::string().swap(const_cast<HttpRequest&>(conn->request()).body);
        conn->readBuffer().reset();
        // Close body_fd if it was opened for large POST bodies
        if (conn->request().body_fd >= 0)
        {
            ::close(conn->request().body_fd);
            const_cast<HttpRequest&>(conn->request()).body_fd = -1;
        }
        _rearmClient(conn->fd());
    }

    _closeCgiJob(result_fd);
}

void EventLoop::_closeCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    // If the stdin writer is still active, close and unregister it first
    // so the child sees EOF and exits promptly.
    _closeCgiStdin(it->second);

    // Reap child process to prevent zombies. Never block the event loop:
    // if the child hasn't exited yet, send SIGKILL and defer the reap.
    if (it->second->child_pid > 0)
    {
        int status;
        pid_t ret = waitpid(it->second->child_pid, &status, WNOHANG);
        if (ret == 0)
        {
            // Child still running — signal it and reap later (non-blocking).
            // A child stuck in uninterruptible sleep (D-state) would otherwise
            // stall the entire event loop if we used waitpid(..., 0) here.
            kill(it->second->child_pid, SIGKILL);
            _pending_reap.push_back(std::make_pair(it->second->child_pid,
                                                     std::time(NULL)));
        }
    }

    _unregisterEventFd(result_fd);
    ::close(result_fd);
    delete it->second;
    _cgi_jobs.erase(it);
}

void EventLoop::_closeCgiJobsForClient(int client_fd)
{
    Connection* conn = _manager->get(client_fd);
    std::vector<int> to_close;
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        if (it->second->client_fd == client_fd)
            to_close.push_back(it->first);
    }

    for (size_t i = 0; i < to_close.size(); ++i)
        _closeCgiJob(to_close[i]);

    // FIX: Free the request body when client disconnects during CGI processing
    if (conn)
    {
        std::string().swap(const_cast<HttpRequest&>(conn->request()).body);
        conn->readBuffer().reset();
        if (conn->request().body_fd >= 0)
        {
            ::close(conn->request().body_fd);
            const_cast<HttpRequest&>(conn->request()).body_fd = -1;
        }
    }
}


// need to check
void EventLoop::_closeTimedOutCgiJobs()
{
    const time_t now = std::time(NULL);
    std::vector<int> timed_out;

    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        if (now - it->second->start_time > it->second->_timeout_seconds)
        {
            timed_out.push_back(it->first);
        }
    }

    for (size_t i = 0; i < timed_out.size(); ++i)
    {
        _failCgiJob(timed_out[i], 504);
    }
}