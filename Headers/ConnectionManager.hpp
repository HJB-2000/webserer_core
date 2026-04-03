// ============================================================
//  ConnectionManager.hpp
//  Single owner of every live Connection.
//  C++98 compliant — lives entirely in this header.
//
//  This is the ONLY place that may:
//    new  a Connection   (addConnection)
//    delete a Connection (closeConnection, _destroy, _destroyAll)
//    erase from the map  (always AFTER delete — never before)
//
//  Fixes applied vs original:
//    1. set_non_blocking() called INSIDE addConnection(), BEFORE epoll ADD
//       (previously done in EventLoop::_handleAccept after registration).
//    2. closeTimedOut() uses each Connection's own config->timeout_seconds
//       instead of a single hard-coded global value.
//
//  Dependency chain:
//    Buffer.hpp  HttpRequest.hpp  ConnectionState.hpp
//        └── Connection.hpp
//                └── ConnectionManager.hpp   ← we are here
// ============================================================
#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <map>
#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>       // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>

// ServerConfig.hpp is provided by Phase 1 (teammate).
// Full definition required here for closeTimedOut() which reads cfg->timeout_seconds.
#include "ServerConfig.hpp"
#include "Connection.hpp"


// ────────────────────────────────────────────────────────────
//  ConnectionManager
// ────────────────────────────────────────────────────────────
class ConnectionManager
{
public:

    // ── ctor / dtor ────────────────────────────────────────

    /**
     * @param epoll_fd  The epoll instance owned by EventLoop.
     *                  ConnectionManager BORROWS it — does not own it.
     */
    explicit ConnectionManager(int epoll_fd)
        : _epoll_fd(epoll_fd)
    {}

    /**
     * Destructor — closes all remaining connections cleanly.
     * Does NOT call epoll_ctl(DEL): the epoll_fd is being closed
     * by EventLoop right after, so all registrations vanish anyway.
     */
    ~ConnectionManager()
    {
        _destroyAll();
    }

    // ── CREATE ─────────────────────────────────────────────

    /**
     * Accept a new client, set it non-blocking, and register with epoll.
     *
     * Order (important):
     *   1. accept()              → get client_fd
     *   2. set_non_blocking()    → MUST happen BEFORE epoll_ctl ADD
     *   3. new Connection(...)   → stable heap object
     *   4. map insert            → manager takes ownership
     *   5. epoll_ctl ADD         → epoll borrows data.ptr
     *
     * @param server_fd  Listening fd that fired EPOLLIN.
     * @param config     Matching ServerConfig (borrowed, never deleted here).
     *
     * @return  New client fd on success.
     *          -1 if accept() returned EAGAIN (edge-trigger: no more clients).
     *
     * Throws std::runtime_error on epoll_ctl failure (programming error).
     */
    int addConnection(int server_fd, const ServerConfig* config)
    {
        // 1. accept
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = ::accept(server_fd,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &addr_len);
        if (client_fd < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -1;   // edge-trigger drained — not an error
            std::cerr << "[ConnectionManager] accept() failed: "
                      << std::strerror(errno) << "\n";
            return -1;
        }

        // 2. set non-blocking BEFORE epoll_ctl ADD
        //    (prevents a blocking fd from sitting in epoll even briefly)
        if (_setNonBlocking(client_fd) < 0)
        {
            std::cerr << "[ConnectionManager] setNonBlocking failed for fd "
                      << client_fd << ": " << std::strerror(errno) << "\n";
            ::close(client_fd);
            return -1;
        }

        // 3. construct Connection
        Connection* conn = new Connection(client_fd, config);

        // 4. map insert — manager takes ownership
        if (_connections.count(client_fd))
        {
            // Duplicate fd should never happen, but guard defensively.
            std::cerr << "[ConnectionManager] fd " << client_fd
                      << " already tracked — closing old connection\n";
            _destroy(client_fd);
        }
        _connections[client_fd] = conn;

        // 5. arm epoll — data.ptr = conn (borrow)
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

    // ── DESTROY ────────────────────────────────────────────

    /**
     * Close a connection and remove it from every data structure.
     *
     * ⚠️  Enforces the golden rule:
     *       epoll DEL first  → stop events
     *       delete           → destructor closes fd
     *       map erase        → pointer gone
     *
     * Any epoll data.ptr for this fd is DANGLING after this call.
     * The event loop MUST return immediately after calling this.
     */
    void closeConnection(int fd)
    {
        if (!_connections.count(fd))
        {
            std::cerr << "[ConnectionManager] closeConnection(" << fd
                      << ") — fd not in map\n";
            return;
        }

        // epoll DEL first: no more events after this point.
        ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);

        _destroy(fd);

        std::cerr << "[ConnectionManager] closed fd " << fd << "\n";
    }

    // ── RE-ARM ─────────────────────────────────────────────

    /**
     * Update epoll interest after a state change.
     *
     * Called by the event loop after:
     *   conn->setWriting()  → needs EPOLLOUT
     *   conn->setReading()  → needs EPOLLIN
     *
     * buildEpollEvent() reads conn->state() so the interest mask
     * is always consistent with the Connection's actual state.
     *
     * Throws std::runtime_error on epoll_ctl failure.
     */
    void rearmEpoll(int fd)
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

    // ── LOOKUP ─────────────────────────────────────────────

    /**
     * Retrieve a Connection by fd.
     * Returns NULL if not found.
     *
     * Prefer using the epoll data.ptr in the hot path (zero map lookup).
     * Use get() only when you have only the fd (e.g. timeout sweep).
     */
    Connection* get(int fd)
    {
        std::map<int, Connection*>::iterator it = _connections.find(fd);
        return (it != _connections.end()) ? it->second : NULL;
    }

    const Connection* get(int fd) const
    {
        std::map<int, Connection*>::const_iterator it = _connections.find(fd);
        return (it != _connections.end()) ? it->second : NULL;
    }

    // ── TIMEOUT SWEEP ──────────────────────────────────────

    /**
     * Close every connection that has been idle beyond its own
     * config->timeout_seconds limit.
     *
     * Uses each Connection's ServerConfig so different virtual hosts
     * can have different timeout settings.
     * Falls back to default_timeout_seconds when config is NULL.
     *
     * Collects stale fds BEFORE closing so the map is never
     * modified during iteration.
     *
     * @param default_timeout_seconds  Fallback when config is unavailable.
     */
    void closeTimedOut(time_t default_timeout_seconds = 60)
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

    // ── DIAGNOSTICS ────────────────────────────────────────
    size_t count() const { return _connections.size(); }
    bool   empty() const { return _connections.empty(); }

private:

    // ── non-copyable ───────────────────────────────────────
    ConnectionManager(const ConnectionManager&);
    ConnectionManager& operator=(const ConnectionManager&);

    // ── helpers ────────────────────────────────────────────

    /** Set a fd to non-blocking mode. */
    static int _setNonBlocking(int fd)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0) return -1;
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    /**
     * Core delete-then-erase sequence.
     * ⚠️  delete BEFORE erase — always, never the other way.
     *     Any epoll data.ptr for this fd is dangling after return.
     */
    void _destroy(int fd)
    {
        delete _connections[fd];    // 1st: ~Connection() → close(fd)
        _connections.erase(fd);     // 2nd: map entry gone
    }

    /** Destroy all connections. Used by destructor on shutdown. */
    void _destroyAll()
    {
        for (std::map<int, Connection*>::iterator it = _connections.begin();
             it != _connections.end(); ++it)
        {
            delete it->second;
        }
        _connections.clear();
    }

    // ── members ────────────────────────────────────────────
    int                        _epoll_fd;     ///< borrowed from EventLoop
    std::map<int, Connection*> _connections;  ///< owns every Connection*
};

#endif // CONNECTION_MANAGER_HPP
