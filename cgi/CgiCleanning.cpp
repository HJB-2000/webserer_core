#include "Headers/EventLoop.hpp"
#include "CgiHandler.hpp"

void EventLoop::_finishCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end()) {
        return;
    }
    CgiJob*     job  = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    if (!job->headers_sent)
    {
        if (conn) {
            _responder.sendError(502, *conn->config(), conn->writeBuffer());
            conn->setWriting();
            _rearmClient(conn->fd());
        }
        _closeCgiJob(result_fd);
        return;
    }

    if (conn && conn->request().method != "HEAD")
        conn->writeBuffer().append("0\r\n\r\n", 5);

    _closeCgiJob(result_fd);
    if (conn && _manager->get(conn->fd()))
    {
        conn->setWriting();
        _rearmClient(conn->fd());
    }
}

void EventLoop::_failCgiJob(int result_fd, int status_code)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end()) return;
    CgiJob*     job  = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    // std::cerr << "[FAIL-DBG] result_fd=" << result_fd
    //           << " status=" << status_code
    //           << " headers_sent=" << job->headers_sent << "\n";

    if (conn) {
        if (!job->headers_sent)
            _responder.sendError(status_code, *conn->config(), conn->writeBuffer());
        else {
            conn->writeBuffer().append("0\r\n\r\n", 5);
            conn->request().headers["connection"] = "close";
        }
        conn->setWriting();
        _rearmClient(conn->fd());
    }
    _closeCgiJob(result_fd);
}

void EventLoop::_closeCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end()) return;
    _closeCgiStdin(it->second);
    if (it->second->child_pid > 0) {
        int status;
        pid_t ret = waitpid(it->second->child_pid, &status, WNOHANG);
        if (ret == 0) {
            kill(it->second->child_pid, SIGKILL);
            _pending_reap.push_back(std::make_pair(it->second->child_pid, std::time(NULL)));
        }
    }
    _unregisterEventFd(result_fd);
    ::close(result_fd);
    delete it->second;
    _cgi_jobs.erase(it);
}

void EventLoop::_closeCgiJobsForClient(int client_fd)
{
    std::vector<int> to_close;
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
        if (it->second->client_fd == client_fd)
            to_close.push_back(it->first);
    for (size_t i = 0; i < to_close.size(); ++i)
        _closeCgiJob(to_close[i]);
}

void EventLoop::_closeTimedOutCgiJobs()
{
    const time_t now = std::time(NULL);
    std::vector<int> timed_out;
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
        if (now - it->second->start_time > it->second->_timeout_seconds)
            timed_out.push_back(it->first);
    for (size_t i = 0; i < timed_out.size(); ++i)
        _failCgiJob(timed_out[i], 504);
}

void EventLoop::_closeCgiStdin(CgiJob* job)
{
    if (!job || job->stdin_fd < 0) return;
    int fd = job->stdin_fd;
    _cgi_stdin_jobs.erase(fd);
    _unregisterEventFd(fd);
    ::close(fd);
    job->stdin_fd     = -1;
    // job->stdin_offset = 0;
    // { std::string _empty; _empty.swap(job->stdin_body); }
}