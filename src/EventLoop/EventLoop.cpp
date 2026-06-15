#include "serverConfig.hpp"
#include "Headers/HttpRequest.hpp"
#include "Headers/EventLoop.hpp"
#include "Headers/HttpParser.hpp"
#include "Headers/ResponseHandler.hpp"
#include <cstdio>        

EventLoop::EventLoop()
    : _epoll_fd(-1)
    , _manager(NULL)
    , _running(false)
    , _stopped(false)
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
    for (std::map<int, CgiJob*>::iterator sit = _cgi_stdin_jobs.begin();
         sit != _cgi_stdin_jobs.end(); ++sit)
    {
        if (sit->first >= 0)
            ::close(sit->first);
    }
    _cgi_stdin_jobs.clear();

    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
            it != _cgi_jobs.end(); ++it)
    {
        CgiJob* job = it->second;
        
        if (it->first >= 0)
            ::close(it->first);
        
        if (job->child_pid > 0)
        {
            kill(job->child_pid, SIGKILL);
            int status;
            waitpid(job->child_pid, &status, 0);
        }
        
        if (job->stdin_fd >= 0)
            ::close(job->stdin_fd);
            
        delete job;
    }
    _cgi_jobs.clear();

    
    for (std::map<int, EventRef*>::iterator it = _event_refs.begin();
         it != _event_refs.end(); ++it)
    {
        Connection* conn = _manager->get(it->second->fd);
        if (conn && conn->request().opened_file > 0)
        {
            
            std::remove(conn->request().tmp_body_path.c_str());
            ::close(conn->request().opened_file);
        }
        delete it->second;
    }
    _event_refs.clear();

    for (size_t i = 0; i < _stale_refs.size(); ++i)
        delete _stale_refs[i];
    _stale_refs.clear();

    for (size_t i = 0; i < _pending_reap.size(); ++i)
    {
        pid_t pid = _pending_reap[i].first;
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == 0)
        {
            kill(pid, SIGKILL);
            for (int j = 0; j < 10; ++j)
            {
                ret = waitpid(pid, &status, WNOHANG);
                if (ret != 0)
                    break;
            }
        }
    }
    _pending_reap.clear();

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
    if (it->second->kind == EV_INVALID) return;
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
}

void EventLoop::_closeTimedOutClients()
{
    std::vector<int> stale = _manager->getTimedOutFds();
    for (size_t i = 0; i < stale.size(); ++i)
    {
        std::cerr << "[EventLoop] timeout — closing client fd " << stale[i] << "\n";
        Connection* conn = _manager->get(stale[i]);
        _responder.sendError(408, *conn->config(), conn->writeBuffer());
        _handleWrite(conn);
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
            continue;
        }

        int   status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == 0)
            remaining.push_back(_pending_reap[i]);
    }
    _pending_reap.swap(remaining);
}


void EventLoop::_dispatch(const epoll_event& ev)
{
    if (_stopped)
        return;
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

    // if (ref->kind == EV_CGI_STDIN)
    // {
    //     _handleCgiStdinEvent(ref->fd, ev.events);
    //     return;
    // }

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
                continue;
            std::cerr << "[EventLoop] epoll_wait error: "
                      << std::strerror(errno) << "\n";
            break;
        }
        if (_stopped)
            break;
        for (int i = 0; i < n; ++i)
            _dispatch(events[i]);

        for (size_t i = 0; i < _stale_refs.size(); ++i)
            delete _stale_refs[i];
        _stale_refs.clear();
        extern void enforce_memory_limit(); // uncomment this to see the role of malloc_trim
        enforce_memory_limit();
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
