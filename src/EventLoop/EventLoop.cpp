#include "serverConfig.hpp"
#include "Headers/HttpRequest.hpp"
#include "Headers/EventLoop.hpp"
#include "Headers/HttpParser.hpp"
#include "Headers/ResponseHandler.hpp"
#include "Headers/CgiJob.hpp"
#include <sys/wait.h>
#include <signal.h>



EventLoop::EventLoop()
    : _epoll_fd(-1)
    , _manager(NULL)
    , _running(false)
{
    _epoll_fd = ::epoll_create(1);
    if (_epoll_fd < 0)
        throw std::runtime_error(
            std::string("[EventLoop] epoll_create failed: ")
            + std::strerror(errno));

    _setCloexec(_epoll_fd, "epoll");

    _manager = new ConnectionManager(_epoll_fd);
}


EventLoop::~EventLoop()
{
    // Clean up all CGI jobs to prevent memory leaks
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        CgiJob* job = it->second;
        if (job->child_pid > 0)
        {
            kill(job->child_pid, SIGKILL);
            int status;
            waitpid(job->child_pid, &status, 0);
        }
        // Close stdin fd if still open
        if (job->stdin_fd >= 0)
            ::close(job->stdin_fd);
        // Unregister stdin event if exists
        std::map<int, CgiJob*>::iterator sit = _cgi_stdin_jobs.find(job->stdin_fd);
        if (sit != _cgi_stdin_jobs.end())
            _cgi_stdin_jobs.erase(sit);
        delete job;
    }
    _cgi_jobs.clear();
    _cgi_stdin_jobs.clear();

    // Clean up all event refs
    for (std::map<int, EventRef*>::iterator it = _event_refs.begin();
         it != _event_refs.end(); ++it)
    {
        delete it->second;
    }
    _event_refs.clear();

    // Clean up stale refs
    for (size_t i = 0; i < _stale_refs.size(); ++i)
        delete _stale_refs[i];
    _stale_refs.clear();

    // Clean up pending reaps - try to reap all remaining zombies
    for (size_t i = 0; i < _pending_reap.size(); ++i)
    {
        pid_t pid = _pending_reap[i].first;
        int status;
        // Try to reap, using WNOHANG to not block
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == 0)
        {
            // Still running after WNOHANG, send SIGKILL
            kill(pid, SIGKILL);
            // Try to reap a few times in a non-blocking way
            for (int j = 0; j < 10; ++j)
            {
                ret = waitpid(pid, &status, WNOHANG);
                if (ret != 0)
                    break;  // Successfully reaped or error
            }
        }
    }
    _pending_reap.clear();

    // Close server fds (only if not already closed by stop())
    for (size_t i = 0; i < _server_fds.size(); ++i) {
        if (_server_fds[i] >= 0) {
            ::close(_server_fds[i]);
            _server_fds[i] = -1;
        }
    }
    _server_fds.clear();

    delete _manager;
    if (_epoll_fd >= 0) {
        ::close(_epoll_fd);
        _epoll_fd = -1;
    }
}

void EventLoop::_unregisterEventFd(int fd)
{
    ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    std::map<int, EventRef*>::iterator it = _event_refs.find(fd);
    if (it != _event_refs.end())
    {
        it->second->kind = EV_INVALID;
        _stale_refs.push_back(it->second);
        _event_refs.erase(it);
    }
}

void EventLoop::_modifyEventFd(int fd, EventKind kind, uint32_t events)
{
    std::map<int, EventRef*>::iterator it = _event_refs.find(fd);
    if (it == _event_refs.end()) return;
    (void)kind;
    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events   = events;
    ev.data.ptr = it->second;
    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        std::cerr << "[EventLoop] epoll_ctl MOD failed for fd " << fd
                  << ": " << std::strerror(errno) << "\n";
        _manager->closeConnection(fd);
    }
}

void EventLoop::_rearmClient(int fd)
{
    Connection* conn = _manager->get(fd);
    if (!conn) return;
    uint32_t events = conn->buildEpollEvent().events;
    _modifyEventFd(fd, EV_CLIENT, events);
}

void EventLoop::_closeClient(int fd)
{
    _closeCgiJobsForClient(fd);
    _unregisterEventFd(fd);
    _manager->closeConnection(fd);
    // delete _event_refs[fd];
}

void EventLoop::_closeTimedOutClients()
{
    std::vector<int> stale = _manager->getTimedOutFds();
    for (size_t i = 0; i < stale.size(); ++i)
    {
        std::cerr << "[EventLoop] timeout — closing client fd " << stale[i] << "\n";
        _closeClient(stale[i]);
    }
}


void EventLoop::_reapPending()
{
    if (_pending_reap.empty())
        return;

    const time_t now = std::time(NULL);
    std::vector< std::pair<pid_t, time_t> > remaining;
    remaining.reserve(_pending_reap.size());
    for (size_t i = 0; i < _pending_reap.size(); ++i)
    {
        pid_t  pid  = _pending_reap[i].first;
        time_t when = _pending_reap[i].second;

        if (now - when > REAP_STALE_SECONDS)
        {
            std::cerr << "[EventLoop] giving up reap for pid " << pid
                      << " after " << REAP_STALE_SECONDS << "s\n";
            continue;  // drop — likely D-state; avoid unbounded growth
        }

        int   status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == 0)
            remaining.push_back(_pending_reap[i]);  // still not exited
        // ret > 0  : reaped
        // ret < 0  : ECHILD or similar — drop it
    }
    _pending_reap.swap(remaining);
}


void EventLoop::_dispatch(const epoll_event& ev)
{
    EventRef* ref = static_cast<EventRef*>(ev.data.ptr);
    if (!ref || ref->kind == EV_INVALID)
        return;

    if (ref->kind == EV_SERVER)
    {
        _handleAccept(ref->fd);
        return;
    }

    if (ref->kind == EV_CGI)
    {
        _handleCgiEvent(ref->fd, ev.events);
        return;
    }

    if (ref->kind == EV_CGI_STDIN)
    {
        _handleCgiStdinEvent(ref->fd, ev.events);
        return;
    }

    if (ref->kind == EV_CLIENT)
    {
        _handleClientEvent(ref->fd, ev.events);
        return;
    }
}

void EventLoop::run()
{
    _running = true;
    epoll_event events[MAX_EVENTS];

    std::cerr << "[EventLoop] starting\n";

    while (_running)
    {
        int n = ::epoll_wait(_epoll_fd, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (n < 0)
        {
            if (errno == EINTR) 
                continue;   // signal interrupted — loop again
            std::cerr << "[EventLoop] epoll_wait error: "
                      << std::strerror(errno) << "\n";
            break;
        }
        // it was removed for some tests  i put it back
        for (int i = 0; i < n; ++i)
            _dispatch(events[i]);

        for (size_t i = 0; i < _stale_refs.size(); ++i)
            delete _stale_refs[i];
        _stale_refs.clear();

        _closeTimedOutClients();
        _closeTimedOutCgiJobs();
        _reapPending();
    }

    std::cerr << "[EventLoop] stopped\n";
}



bool EventLoop::_isServerFd(int fd) const
{
    for (size_t i = 0; i < _server_fds.size(); ++i)
        if (_server_fds[i] == fd) return true;
    return false;
}

const ServerConfig* EventLoop::_configForServer(int fd) const
{
    for (size_t i = 0; i < _server_fds.size(); ++i)
        if (_server_fds[i] == fd) return _server_configs[i];
    return NULL;
}
