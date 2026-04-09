// ============================================================
//  ConnectionManager.hpp
//  Single owner of every live Connection.
//  C++98 compliant.
//
//  Implementation: src/ConnectionManager.cpp
//
//  This is the ONLY place that may:
//    new  a Connection   (addConnection)
//    delete a Connection (closeConnection, _destroy, _destroyAll)
//    erase from the map  (always AFTER delete — never before)
//
//  Dependency chain:
//    Buffer.hpp  HttpRequest.hpp  ConnectionState.hpp
//        └── Connection.hpp
//                └── ConnectionManager.hpp   ← we are here
// ============================================================
#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <map>
#include <ctime>
#include <cstddef>

#include "serverConfig.hpp"
#include "Connection.hpp"


// ────────────────────────────────────────────────────────────
//  ConnectionManager
// ────────────────────────────────────────────────────────────
class ConnectionManager
{
public:

    explicit ConnectionManager(int epoll_fd);
    ~ConnectionManager();

    // ── CREATE ─────────────────────────────────────────────
    // Returns new client fd, or -1 on EAGAIN / error.
    // Throws std::runtime_error on epoll_ctl failure.
    int addConnection(int server_fd, const ServerConfig* config);

    // ── DESTROY ────────────────────────────────────────────
    // ⚠️  conn pointer is dangling after return.
    void closeConnection(int fd);

    // ── RE-ARM ─────────────────────────────────────────────
    // Update epoll interest after a state change.
    // Throws std::runtime_error on epoll_ctl failure.
    void rearmEpoll(int fd);

    // ── LOOKUP ─────────────────────────────────────────────
    // Returns NULL if fd not found.
    Connection*       get(int fd);
    const Connection* get(int fd) const;

    // ── TIMEOUT SWEEP ──────────────────────────────────────
    void closeTimedOut(time_t default_timeout_seconds = 60);

    // ── DIAGNOSTICS ────────────────────────────────────────
    size_t count() const;
    bool   empty() const;

private:

    // ── non-copyable ───────────────────────────────────────
    ConnectionManager(const ConnectionManager&);
    ConnectionManager& operator=(const ConnectionManager&);

    // ── private helpers ────────────────────────────────────
    static int _setNonBlocking(int fd);
    void       _destroy(int fd);
    void       _destroyAll();

    // ── members ────────────────────────────────────────────
    int                        _epoll_fd;
    int                        _max_connections;
    std::map<int, Connection*> _connections;
};

#endif // CONNECTION_MANAGER_HPP
