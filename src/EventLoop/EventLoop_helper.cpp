#include "Headers/EventLoop.hpp"

bool EventLoop::_setCloexec(int fd, const char* label)
{
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
    {
        std::cerr << "[EventLoop] FD_CLOEXEC failed on " << label
                  << " fd " << fd << ": " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

void EventLoop::_registerEventFd(int fd, EventKind kind, uint32_t events)
{
    //we have this problem of leaks
    EventRef* ref = NULL;
    try {
        ref = new EventRef(kind, fd);
    }
    catch (const std::exception& e)
    {
        throw ;
    }
    _event_refs[fd] = ref;

    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = ref;

    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error(std::string("epoll_ctl ADD failed: ")
            + std::strerror(errno));
}


void EventLoop::addServerSocket(int server_fd, const ServerConfig* config)
{
    _setCloexec(server_fd, "server");

    _server_fds.push_back(server_fd);
    _server_configs.push_back(config);
    _registerEventFd(server_fd, EV_SERVER, EPOLLIN | EPOLLET);
    std::cerr << "[EventLoop] listening on fd " << server_fd << "\n";
}


void EventLoop::stop() {
    _running = false;
    for (size_t i = 0; i < _server_fds.size(); ++i) {
        int fd = _server_fds[i];
        
        if (fd >= 0) {
            epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            if (_event_refs.count(fd) && _event_refs[fd] != NULL) {
                delete _event_refs[fd];
                _event_refs[fd] = NULL;
                _event_refs.erase(fd);
            }
            
        }
    }

    if (_epoll_fd >= 0) {
        close(_epoll_fd);
        _epoll_fd = -1;
    }
}


int EventLoop::setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

