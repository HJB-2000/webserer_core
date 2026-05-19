// ============================================================
//  ConnectionManager.cpp — ConnectionManager implementations
// ============================================================
#include "serverConfig.hpp"
#include "Headers/ConnectionManager.hpp"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <ctime>
#include <sstream>
#include "Headers/Logger.hpp"
#include <arpa/inet.h>   // only for ntohl, ntohs etc., not for inet_ntop
static std::string addrToString(const struct sockaddr_storage& addr)
{
    // IPv4 only
    const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&addr);
    uint32_t ip = ntohl(sin->sin_addr.s_addr);
    std::ostringstream oss;
    oss << ((ip >> 24) & 0xFF) << '.'
        << ((ip >> 16) & 0xFF) << '.'
        << ((ip >> 8)  & 0xFF) << '.'
        << (ip & 0xFF);
    return oss.str();
}

// ── ctor / dtor ──────────────────────────────────────────────

ConnectionManager::ConnectionManager(int epoll_fd)
    : _epoll_fd(epoll_fd)
    , _max_connections(std::max(readSomaxconn(), 10000))
{
    std::cerr << "[ConnectionManager] max connections: " << _max_connections << "\n";
}

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

    if (static_cast<int>(_connections.size()) >= _max_connections)
        return -1;  // at capacity — let kernel queue new SYNs

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
    
    // Connection* conn = NULL;
    // try {
    //     conn = new Connection(client_fd, config);   
    // }
    // catch (const std::exception& ex)
    // {
    //     throw;
    // }

    std::string client_ip = addrToString(client_addr); // getting the ip_client from the client_addr
    conn->setClientIp(client_ip); // storing the remote ip address of client
    if (_connections.count(client_fd))
    {
        std::cerr << "[ConnectionManager] fd " << client_fd
                  << " already tracked — closing old connection\n";
        _destroy(client_fd);
    }
    _connections[client_fd] = conn;

    // epoll registration is handled by EventLoop::_handleAccept via _registerEventFd
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
    {  
        std::cerr << "[ConnectionManager] epoll_ctl MOD failed for fd " << fd  
                  << ": " << std::strerror(errno) << "\n";  
        closeConnection(fd);  
    };
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

// ── getTimedOutFds ───────────────────────────────────────────
//
// Returns fds of timed-out connections without closing them.
// EventLoop calls _closeClient() on each to properly clean up
// EventRef, CGI jobs, and the connection itself.
std::vector<int> ConnectionManager::getTimedOutFds(time_t default_timeout_seconds)
{
    std::vector<int> stale;

    for (std::map<int, Connection*>::iterator it = _connections.begin();
         it != _connections.end(); ++it)
    {
        Connection*         conn = it->second;
        const ServerConfig* cfg  = conn->config();
        // CGI-running connections are managed by _closeTimedOutCgiJobs()
        // Don't let the connection timeout race against the CGI timeout
        if (conn->state() == CSTATE_CGI_RUNNING)
            continue;
        time_t limit = (cfg != NULL)
            ? static_cast<time_t>(cfg->get_timeout_seconds())
            : default_timeout_seconds;

        if (conn->isTimedOut(limit))
            stale.push_back(it->first);
    }

    return stale;
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
