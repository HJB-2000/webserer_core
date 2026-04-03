// ============================================================
//  ConnectionManager.cpp — ConnectionManager implementations
// ============================================================
#include "Headers/ServerConfig.hpp"
#include "Headers/ConnectionManager.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <ctime>

// ── ctor / dtor ──────────────────────────────────────────────

ConnectionManager::ConnectionManager(int epoll_fd)
    : _epoll_fd(epoll_fd)
{}

ConnectionManager::~ConnectionManager()
{
    _destroyAll();
}

// ── addConnection ────────────────────────────────────────────
//
// Order (important):
//   1. accept()           → get client_fd
//   2. _setNonBlocking()  → MUST happen BEFORE epoll_ctl ADD
//   3. new Connection()   → stable heap object
//   4. map insert         → manager takes ownership
//   5. epoll_ctl ADD      → epoll borrows data.ptr
int ConnectionManager::addConnection(int server_fd, const ServerConfig* config)
{
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(server_fd,
                             reinterpret_cast<struct sockaddr*>(&client_addr),
                             &addr_len);
    if (client_fd < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return -1;
        std::cerr << "[ConnectionManager] accept() failed: "
                  << std::strerror(errno) << "\n";
        return -1;
    }

    if (_setNonBlocking(client_fd) < 0)
    {
        std::cerr << "[ConnectionManager] setNonBlocking failed for fd "
                  << client_fd << ": " << std::strerror(errno) << "\n";
        ::close(client_fd);
        return -1;
    }

    Connection* conn = new Connection(client_fd, config);

    if (_connections.count(client_fd))
    {
        std::cerr << "[ConnectionManager] fd " << client_fd
                  << " already tracked — closing old connection\n";
        _destroy(client_fd);
    }
    _connections[client_fd] = conn;

    epoll_event ev = conn->buildEpollEvent();
    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
    {
        _destroy(client_fd);
        throw std::runtime_error(
            std::string("[ConnectionManager] epoll_ctl ADD failed: ")
            + std::strerror(errno));
    }

    std::cerr << "[ConnectionManager] accepted fd " << client_fd << "\n";
    return client_fd;
}

// ── closeConnection ──────────────────────────────────────────
//
// ⚠️  epoll DEL first → delete → erase. Never the other way.
//    Any epoll data.ptr for this fd is dangling after return.
void ConnectionManager::closeConnection(int fd)
{
    if (!_connections.count(fd))
    {
        std::cerr << "[ConnectionManager] closeConnection(" << fd
                  << ") — fd not in map\n";
        return;
    }

    ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    _destroy(fd);
    std::cerr << "[ConnectionManager] closed fd " << fd << "\n";
}

// ── rearmEpoll ───────────────────────────────────────────────

void ConnectionManager::rearmEpoll(int fd)
{
    std::map<int, Connection*>::iterator it = _connections.find(fd);
    if (it == _connections.end())
    {
        std::cerr << "[ConnectionManager] rearmEpoll(" << fd
                  << ") — fd not in map\n";
        return;
    }

    epoll_event ev = it->second->buildEpollEvent();
    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
        throw std::runtime_error(
            std::string("[ConnectionManager] epoll_ctl MOD failed: ")
            + std::strerror(errno));
}

// ── get ──────────────────────────────────────────────────────

Connection* ConnectionManager::get(int fd)
{
    std::map<int, Connection*>::iterator it = _connections.find(fd);
    return (it != _connections.end()) ? it->second : NULL;
}

const Connection* ConnectionManager::get(int fd) const
{
    std::map<int, Connection*>::const_iterator it = _connections.find(fd);
    return (it != _connections.end()) ? it->second : NULL;
}

// ── closeTimedOut ────────────────────────────────────────────
//
// Collects stale fds BEFORE closing so the map is never
// modified during iteration.
void ConnectionManager::closeTimedOut(time_t default_timeout_seconds)
{
    std::vector<int> stale;

    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        Connection*         conn = it->second;
        const ServerConfig* cfg  = conn->config();

        time_t limit = (cfg != NULL)
            ? static_cast<time_t>(cfg->timeout_seconds)
            : default_timeout_seconds;

        if (conn->isTimedOut(limit))
            stale.push_back(it->first);
    }

    for (std::vector<int>::iterator it = stale.begin();
         it != stale.end(); ++it)
    {
        std::cerr << "[ConnectionManager] timeout — closing fd " << *it << "\n";
        closeConnection(*it);
    }
}

// ── count / empty ────────────────────────────────────────────

size_t ConnectionManager::count() const { return _connections.size(); }
bool   ConnectionManager::empty() const { return _connections.empty(); }

// ── private helpers ──────────────────────────────────────────

int ConnectionManager::_setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ⚠️  delete BEFORE erase — always, never the other way.
void ConnectionManager::_destroy(int fd)
{
    delete _connections[fd];
    _connections.erase(fd);
}

void ConnectionManager::_destroyAll()
{
    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        delete it->second;
    }
    _connections.clear();
}
