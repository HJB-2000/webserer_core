// ============================================================
//  ConnectionManager.hpp
//  Owns the lifetime of every live Connection.
//  C++98 compliant — lives entirely in this header.
//
//  This is the ONLY place allowed to:
//    - new  a Connection   (addConnection)
//    - delete a Connection (closeConnection)
//    - erase from the map  (closeConnection, always AFTER delete)
//
//  Every other part of the server reaches connections through
//  get() or the epoll data.ptr borrow — never through raw new/delete.
//
//  Dependency chain:
//    Buffer.hpp
//    HttpRequest.hpp
//    ConnectionState.hpp
//    Connection.hpp
//        └── ConnectionManager.hpp   ← we are here
// ============================================================
#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <map>           // std::map
#include <vector>        // std::vector (for timeout sweep results)
#include <sys/epoll.h>   // epoll_ctl, epoll_event, EPOLL_CTL_ADD
                         // EPOLL_CTL_MOD, EPOLL_CTL_DEL
#include <sys/socket.h>  // accept(), sockaddr
#include <unistd.h>      // close()
#include <cerrno>        // errno
#include <cstring>       // strerror
#include <stdexcept>     // std::runtime_error
#include <iostream>      // std::cerr (debug logging)

#include "Connection.hpp"

// Forward declaration — full definition in its own header
class ServerConfig;


// ────────────────────────────────────────────────────────────
//  ConnectionManager
//
//  Single owner of the connections map.
//  The epoll file descriptor is stored here too so that
//  addConnection / closeConnection can arm / disarm epoll
//  atomically with map insertion / removal.
//
//  Usage in the server main loop:
//
//    ConnectionManager cm(epoll_fd);
//
//    // accept branch
//    cm.addConnection(client_fd, &config);
//
//    // epoll event branch — get Connection from data.ptr
//    Connection* conn = static_cast<Connection*>(ev.data.ptr);
//    // ... do I/O ...
//    if (should_close)
//        cm.closeConnection(conn->fd());
//
//    // periodic sweep
//    cm.closeTimedOut(60);
// ────────────────────────────────────────────────────────────
class ConnectionManager
{
public:

    // ── ctor / dtor ────────────────────────────────────────

    /**
     * @param epoll_fd  The epoll instance created in main().
     *                  ConnectionManager borrows it — does NOT own it.
     *                  The server closes it on shutdown.
     */
    explicit ConnectionManager(int epoll_fd)
        : _epoll_fd(epoll_fd)
    {}

    /**
     * Destructor — closes ALL remaining connections cleanly.
     * Called on server shutdown.
     * Walks the map, deletes every Connection (closes fds),
     * then clears the map.
     *
     * ⚠️  We do NOT call epoll_ctl(DEL) here because the epoll fd
     *     itself is being closed by the server right after — all
     *     registrations vanish automatically.
     */
    ~ConnectionManager()
    {
        _destroyAll();
    }

    // ── CREATE path ────────────────────────────────────────

    /**
     * Accept a new client and register it with epoll.
     *
     * Flow (matches blueprint CREATE path):
     *   accept() → new Connection(fd, config)
     *            → insert into map
     *            → arm epoll EPOLLIN | EPOLLET
     *            → store &conn in epoll_event.data.ptr
     *
     * @param server_fd   The listening fd that fired EPOLLIN.
     * @param config      The ServerConfig matched for this listener.
     *                    Pointer is borrowed — ConnectionManager
     *                    never owns or deletes it.
     *
     * @return  The new client fd on success.
     *          -1 if accept() failed (EAGAIN = no more clients,
     *          not an error in edge-trigger mode).
     *
     * Throws std::runtime_error if epoll_ctl fails — that is a
     * programming error, not a recoverable runtime condition.
     */
    int addConnection(int server_fd, const ServerConfig* config)
    {
        // ── 1. accept ─────────────────────────────────────
        struct sockaddr_storage client_addr;
        socklen_t               addr_len = sizeof(client_addr);

        int client_fd = ::accept(server_fd,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &addr_len);
        if (client_fd < 0)
        {
            // EAGAIN / EWOULDBLOCK = edge-trigger drained, not an error
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return -1;

            std::cerr << "[ConnectionManager] accept() failed: "
                      << std::strerror(errno) << "\n";
            return -1;
        }

        // ── 2. new Connection ─────────────────────────────
        //  Heap-allocate so epoll can borrow a stable pointer.
        //  The map takes ownership immediately.
        Connection* conn = new Connection(client_fd, config);

        // ── 3. insert into map ────────────────────────────
        //  If somehow we already have this fd (shouldn't happen
        //  but be safe) close the old one first.
        if (_connections.count(client_fd))
        {
            std::cerr << "[ConnectionManager] fd " << client_fd
                      << " already in map — closing old connection\n";
            _destroy(client_fd);
        }
        _connections[client_fd] = conn;

        // ── 4. arm epoll ──────────────────────────────────
        //  buildEpollEvent() sets data.ptr = conn  (the borrow)
        //  and events = EPOLLIN | EPOLLET for CS_READING.
        epoll_event ev = conn->buildEpollEvent();

        if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0)
        {
            // epoll_ctl failure is a hard error — clean up and throw
            _destroy(client_fd);
            throw std::runtime_error(
                std::string("[ConnectionManager] epoll_ctl ADD failed: ")
                + std::strerror(errno));
        }

        std::cerr << "[ConnectionManager] accepted fd " << client_fd << "\n";
        return client_fd;
    }

    // ── DESTROY path ───────────────────────────────────────

    /**
     * Close a connection and remove it from every data structure.
     *
     * ⚠️  ENFORCES the blueprint rule:
     *       delete BEFORE erase — always together, never separate
     *
     * Flow (matches blueprint DESTROY path):
     *   epoll_ctl(DEL)          ← stop receiving events first
     *   delete connections[fd]  ← destructor closes the fd
     *   connections.erase(fd)   ← map entry gone
     *
     * After this call, any pointer obtained from epoll's data.ptr
     * for this fd is DANGLING.  The epoll loop must not use it.
     *
     * @param fd  The client fd to close.
     *            No-op (with a warning) if fd is not in the map.
     */
    void closeConnection(int fd)
    {
        if (!_connections.count(fd))
        {
            std::cerr << "[ConnectionManager] closeConnection("
                      << fd << ") — fd not in map (already closed?)\n";
            return;
        }

        // ── 1. disarm epoll first ─────────────────────────
        //  Prevents stale events firing after the object is gone.
        //  Ignore errors — fd may already be closed / removed.
        ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);

        // ── 2. delete then erase ──────────────────────────
        _destroy(fd);

        std::cerr << "[ConnectionManager] closed fd " << fd << "\n";
    }

    // ── re-arm epoll after state change ────────────────────

    /**
     * Re-arm epoll for a connection whose state just changed.
     *
     * Called by the server loop after:
     *   conn->setWriting()    → need EPOLLOUT now
     *   conn->setReading()    → back to EPOLLIN
     *
     * buildEpollEvent() reads conn->state() and sets events
     * accordingly, keeping data.ptr = conn (borrow intact).
     *
     * @param fd  Must be in the map — asserted via the find check.
     *
     * Throws std::runtime_error on epoll_ctl failure.
     */
    void rearmEpoll(int fd)
    {
        std::map<int, Connection*>::iterator it = _connections.find(fd);
        if (it == _connections.end())
        {
            std::cerr << "[ConnectionManager] rearmEpoll("
                      << fd << ") — fd not in map\n";
            return;
        }

        Connection* conn = it->second;
        epoll_event ev   = conn->buildEpollEvent();

        if (::epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0)
        {
            throw std::runtime_error(
                std::string("[ConnectionManager] epoll_ctl MOD failed: ")
                + std::strerror(errno));
        }
    }

    // ── lookup ─────────────────────────────────────────────

    /**
     * Retrieve a Connection by fd.
     * Returns NULL if not found — caller must check.
     *
     * Prefer going through epoll's data.ptr in the hot path
     * (zero map lookup overhead).  Use get() only when you have
     * only the fd available (e.g. timeout sweep, signal handler).
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

    // ── timeout sweep ──────────────────────────────────────

    /**
     * Walk all connections and close any that have been idle
     * longer than timeout_seconds.
     *
     * Collects stale fds into a local vector FIRST, then closes
     * them — never modifies the map while iterating it.
     *
     * @param timeout_seconds  From ServerConfig (e.g. 60).
     *
     * Called by the server loop after every epoll_wait() cycle
     * or on a periodic timer signal.
     */
    void closeTimedOut(time_t timeout_seconds)
    {
        // Collect stale fds first — safe iteration
        std::vector<int> stale;

        for (std::map<int, Connection*>::iterator it = _connections.begin();
             it != _connections.end(); ++it)
        {
            if (it->second->isTimedOut(timeout_seconds))
                stale.push_back(it->first);
        }

        // Now close them — map is not being iterated
        for (std::vector<int>::iterator it = stale.begin();
             it != stale.end(); ++it)
        {
            std::cerr << "[ConnectionManager] timeout on fd " << *it << "\n";
            closeConnection(*it);
        }
    }

    // ── diagnostics ────────────────────────────────────────

    /** Number of currently active connections. */
    size_t count() const { return _connections.size(); }

    /** True when no connections are tracked. */
    bool   empty() const { return _connections.empty(); }

private:

    // ── non-copyable ───────────────────────────────────────
    //  Copying would duplicate raw pointers — instant double-delete.
    ConnectionManager(const ConnectionManager&);
    ConnectionManager& operator=(const ConnectionManager&);

    // ── internal helpers ───────────────────────────────────

    /**
     * Core delete-before-erase sequence.
     * Called by closeConnection() and _destroyAll().
     *
     * ⚠️  NEVER call erase() without delete() first.
     *     NEVER call delete() without erase() after.
     *     These two lines are glued together by design.
     *
     * @param fd  Must exist in _connections — callers guarantee this.
     */
    void _destroy(int fd)
    {
        delete _connections[fd];   // 1st: destructor → close(fd)
        _connections.erase(fd);    // 2nd: map entry gone
        // ⚠️  Any epoll data.ptr for this fd is now dangling
    }

    /**
     * Destroy every connection in the map.
     * Used by the destructor on server shutdown.
     * Does NOT call epoll_ctl(DEL) — epoll_fd is being closed anyway.
     */
    void _destroyAll()
    {
        for (std::map<int, Connection*>::iterator it = _connections.begin();
             it != _connections.end(); ++it)
        {
            delete it->second;   // close(fd) via ~Connection()
        }
        _connections.clear();
    }

    // ── members ────────────────────────────────────────────

    int                       _epoll_fd;     ///< borrowed — server owns it
    std::map<int, Connection*> _connections; ///< owns every Connection*
};

#endif // CONNECTION_MANAGER_HPP