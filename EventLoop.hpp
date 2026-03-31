// ============================================================
//  EventLoop.hpp
//  The core epoll dispatch loop.
//  C++98 compliant — lives entirely in this header.
//
//  This is where every component we built comes together:
//
//    ConnectionManager  → owns all Connection objects
//    Connection         → owns Buffer x2, HttpRequest, state
//    epoll              → tells us which fd fired and why
//
//  Responsibilities:
//  ─────────────────
//  1. Create and own the epoll file descriptor
//  2. Register server (listening) fds
//  3. Run the event loop  (run())
//  4. Dispatch each event to the correct handler
//  5. Never touch a Connection* after closeConnection()
//
//  What this class does NOT do (filled in by server logic later):
//  ──────────────────────────────────────────────────────────────
//  • HTTP parsing       → HttpParser
//  • Response building  → ResponseHandler
//  • CGI execution      → CgiHandler
//  These are called as stubs here — you plug in real logic next.
//
//  Dependency chain:
//    Buffer.hpp
//    HttpRequest.hpp
//    ConnectionState.hpp
//    Connection.hpp
//    ConnectionManager.hpp
//        └── EventLoop.hpp   ← we are here
// ============================================================
#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP

#include <sys/epoll.h>   // epoll_create, epoll_ctl, epoll_wait
#include <sys/socket.h>  // accept, send, recv
#include <unistd.h>      // close
#include <fcntl.h>       // fcntl, F_SETFL, O_NONBLOCK
#include <cerrno>        // errno
#include <cstring>       // strerror
#include <stdexcept>     // std::runtime_error
#include <iostream>      // std::cerr
#include <vector>        // std::vector (event batch)
#include <csignal>       // sig_atomic_t

#include "ConnectionManager.hpp"
#include "Connection.hpp"

// Forward declarations — plugged in by server logic
class ServerConfig;
class HttpParser;
class ResponseHandler;


// ────────────────────────────────────────────────────────────
//  EventLoop
//
//  One instance lives in main().
//  Owns the epoll fd and the ConnectionManager.
//
//  Typical setup:
//
//    EventLoop loop;
//    loop.addServerSocket(listen_fd, &config);
//    loop.run();                // blocks until stop() is called
// ────────────────────────────────────────────────────────────
class EventLoop
{
public:

    // ── constants ──────────────────────────────────────────
    static const int    MAX_EVENTS       = 64;   ///< epoll batch size
    static const time_t TIMEOUT_SECONDS  = 60;   ///< idle connection TTL
    static const int    EPOLL_TIMEOUT_MS = 1000; ///< epoll_wait block limit

    // ── ctor / dtor ────────────────────────────────────────

    /**
     * Creates the epoll instance.
     * Throws std::runtime_error if epoll_create fails.
     */
    EventLoop()
        : _epoll_fd(-1)
        , _manager(NULL)
        , _running(false)
    {
        // epoll_create1 not in C++98 env guarantee — use epoll_create
        _epoll_fd = ::epoll_create(1);   // argument ignored but must be > 0
        if (_epoll_fd < 0)
            throw std::runtime_error(
                std::string("[EventLoop] epoll_create failed: ")
                + std::strerror(errno));

        // ConnectionManager borrows _epoll_fd
        _manager = new ConnectionManager(_epoll_fd);
    }

    /**
     * Destructor — closes epoll fd, destroys ConnectionManager
     * (which closes every live connection cleanly).
     */
    ~EventLoop()
    {
        delete _manager;
        if (_epoll_fd >= 0)
            ::close(_epoll_fd);
    }

    // ── server socket registration ─────────────────────────

    /**
     * Register a listening (server) socket with epoll.
     *
     * Server sockets are treated differently from client sockets:
     *   - They are not Connection objects
     *   - Their fd fires EPOLLIN when a client is ready to accept
     *   - We store them separately so the event loop can identify
     *     them quickly (fd == one of _server_fds)
     *
     * @param server_fd  Already bound, listening, non-blocking fd.
     * @param config     The ServerConfig associated with this listener.
     *                   Stored so accept() can pass it to new Connections.
     *
     * ⚠️  server_fd MUST be set non-blocking before calling this.
     *     Use setNonBlocking(fd) below.
     */
    void addServerSocket(int server_fd, const ServerConfig* config)
    {
        // Store fd → config mapping for the accept() branch
        _server_fds.push_back(server_fd);
        _server_configs.push_back(config);

        // Register with epoll — data.fd (not data.ptr) for server sockets
        // because they have no Connection object to point to
        epoll_event ev;
        ev.events   = EPOLLIN | EPOLLET;
        ev.data.fd  = server_fd;

        if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
            throw std::runtime_error(
                std::string("[EventLoop] epoll_ctl ADD server fd failed: ")
                + std::strerror(errno));

        std::cerr << "[EventLoop] listening on fd " << server_fd << "\n";
    }

    // ── main loop ──────────────────────────────────────────

    /**
     * Block and dispatch events until stop() is called.
     *
     * Each iteration:
     *   1. epoll_wait  → fills _events batch
     *   2. Dispatch    → one of four handlers per event
     *   3. Sweep       → close idle connections
     *
     * The four dispatch paths mirror the blueprint exactly:
     *   fd is server fd   → _handleAccept()
     *   EPOLLERR/EPOLLHUP → _handleError()
     *   EPOLLIN           → _handleRead()
     *   EPOLLOUT          → _handleWrite()
     */
    void run()
    {
        _running = true;
        epoll_event events[MAX_EVENTS];

        std::cerr << "[EventLoop] starting\n";

        while (_running)
        {
            // ── 1. wait ───────────────────────────────────
            int n = ::epoll_wait(_epoll_fd,
                                 events,
                                 MAX_EVENTS,
                                 EPOLL_TIMEOUT_MS);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;   // signal interrupted — loop again
                std::cerr << "[EventLoop] epoll_wait error: "
                          << std::strerror(errno) << "\n";
                break;
            }

            // ── 2. dispatch ───────────────────────────────
            for (int i = 0; i < n; ++i)
                _dispatch(events[i]);

            // ── 3. periodic sweep ─────────────────────────
            _manager->closeTimedOut(TIMEOUT_SECONDS);
        }

        std::cerr << "[EventLoop] stopped\n";
    }

    /** Signal the loop to exit cleanly after the current iteration. */
    void stop() { _running = false; }

    // ── utility ────────────────────────────────────────────

    /**
     * Set a file descriptor to non-blocking mode.
     * Must be called on every fd BEFORE adding it to epoll.
     * Both server sockets and client sockets need this.
     *
     * @return  0 on success, -1 on failure (errno set).
     */
    static int setNonBlocking(int fd)
    {
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return -1;
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    /** Access the ConnectionManager (e.g. for signal handlers). */
    ConnectionManager& manager() { return *_manager; }

private:

    // ── non-copyable ───────────────────────────────────────
    EventLoop(const EventLoop&);
    EventLoop& operator=(const EventLoop&);

    // ── dispatch ───────────────────────────────────────────

    /**
     * Route a single epoll_event to the correct handler.
     *
     * Decision tree (matches blueprint exactly):
     *
     *  ┌─ is it a server fd?
     *  │    YES → _handleAccept()
     *  │    NO  ─┬─ EPOLLERR | EPOLLHUP → _handleError()
     *  │         ├─ EPOLLIN             → _handleRead()
     *  │         └─ EPOLLOUT            → _handleWrite()
     *  │
     *  └─ data.ptr is the borrowed Connection pointer
     *     (only valid for client fds — guaranteed by addServerSocket
     *      using data.fd and addConnection using data.ptr)
     *
     * ⚠️  After any call that may closeConnection(), we MUST NOT
     *     use conn again.  Each handler returns immediately after
     *     a close to enforce this.
     */
    void _dispatch(const epoll_event& ev)
    {
        // ── server fd branch ──────────────────────────────
        // Server fds were registered with data.fd, not data.ptr
        if (_isServerFd(ev.data.fd))
        {
            _handleAccept(ev.data.fd);
            return;
        }

        // ── client fd branch ─────────────────────────────
        // Recover the Connection pointer epoll has been borrowing
        // data.ptr is valid ONLY if the Connection is still in the map
        Connection* conn = static_cast<Connection*>(ev.data.ptr);

        // Safety: verify the pointer is still live in the manager
        // (edge case: two events for same fd in one batch after close)
        if (!conn || !_manager->get(conn->fd()))
        {
            std::cerr << "[EventLoop] stale event — connection already gone\n";
            return;
        }

        // ── error / hangup ────────────────────────────────
        if (ev.events & (EPOLLERR | EPOLLHUP))
        {
            _handleError(conn);
            return;   // ⚠️ conn is dangling after this
        }

        // ── readable ──────────────────────────────────────
        if (ev.events & EPOLLIN)
        {
            _handleRead(conn);
            // conn may be dangling if read triggered close
            return;
        }

        // ── writable ─────────────────────────────────────
        if (ev.events & EPOLLOUT)
        {
            _handleWrite(conn);
            // conn may be dangling if write triggered close
            return;
        }
    }

    // ── handlers ───────────────────────────────────────────

    /**
     * ACCEPT handler.
     * fd == one of _server_fds → EPOLLIN means a client is waiting.
     *
     * In edge-trigger mode we must loop until EAGAIN to drain
     * all pending connections in one wake-up.
     *
     * Flow (blueprint CREATE path):
     *   accept() → new Connection → insert map → arm epoll EPOLLIN
     */
    void _handleAccept(int server_fd)
    {
        const ServerConfig* config = _configForServer(server_fd);

        // Edge-trigger: drain all pending accepts
        while (true)
        {
            int client_fd = _manager->addConnection(server_fd, config);
            if (client_fd < 0)
                break;  // EAGAIN — no more clients right now

            // Set non-blocking — required for edge-trigger correctness
            if (setNonBlocking(client_fd) < 0)
            {
                std::cerr << "[EventLoop] setNonBlocking failed for fd "
                          << client_fd << "\n";
                _manager->closeConnection(client_fd);
            }
        }
    }

    /**
     * ERROR handler.
     * EPOLLERR or EPOLLHUP fired on a client fd.
     *
     * No attempt at partial reads — just close immediately.
     * Blueprint: EPOLLERR/EPOLLHUP → closeConnection()
     *
     * ⚠️  conn is dangling after this function returns.
     *     Caller must return immediately.
     */
    void _handleError(Connection* conn)
    {
        const int fd = conn->fd();   // save fd BEFORE delete
        std::cerr << "[EventLoop] error/hup on fd " << fd << "\n";
        _manager->closeConnection(fd);
        // conn is now dangling — do not touch
    }

    /**
     * READ handler.
     * EPOLLIN fired on a client fd.
     *
     * Flow (blueprint EPOLLIN path):
     *   recv() → append to read_buffer
     *          → [stub] Parser runs on read_buffer
     *          → check parse_state
     *              PS_COMPLETE → setProcessing() → [stub] respond
     *              PS_ERROR    → [stub] send 400 → setClosing()
     *          → if peer closed (recv == 0) → closeConnection()
     *
     * Edge-trigger: loop until EAGAIN to drain the kernel buffer.
     *
     * ⚠️  conn may be dangling after this if we close.
     *     We save fd first and check _manager->get() before reuse.
     */
    void _handleRead(Connection* conn)
    {
        const int fd = conn->fd();   // save before any possible close

        // Edge-trigger read loop — drain until EAGAIN
        while (true)
        {
            ssize_t n = conn->recv();

            if (n == 0)
            {
                // Peer closed connection cleanly
                std::cerr << "[EventLoop] fd " << fd
                          << " closed by peer\n";
                _manager->closeConnection(fd);
                return;   // ⚠️ conn dangling
            }

            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;   // edge-trigger drained — done for now

                // Real error
                std::cerr << "[EventLoop] recv error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _manager->closeConnection(fd);
                return;   // ⚠️ conn dangling
            }

            // ── [STUB] run parser on read_buffer ──────────
            // TODO: replace with real HttpParser call
            //   _parser.feed(conn->readBuffer(), conn->request());
            _stubParse(conn);

            // ── check parse result ────────────────────────
            if (conn->request().parse_state == PS_ERROR)
            {
                // TODO: replace stub with real 400 response builder
                std::cerr << "[EventLoop] parse error on fd "
                          << fd << " — sending 400\n";
                _stubSend400(conn);
                _manager->closeConnection(fd);
                return;   // ⚠️ conn dangling
            }

            if (conn->request().parse_state == PS_COMPLETE)
            {
                conn->setProcessing();

                // ── [STUB] build response ─────────────────
                // TODO: replace with real ResponseHandler call
                //   _responder.handle(conn->request(),
                //                     conn->writeBuffer(),
                //                     *conn->config());
                _stubBuildResponse(conn);

                conn->setWriting();
                _manager->rearmEpoll(fd);

                // Do NOT continue reading — wait for EPOLLOUT
                return;
            }
            // PS_IDLE / PS_HEADERS / PS_BODY → keep reading
        }
    }

    /**
     * WRITE handler.
     * EPOLLOUT fired on a client fd.
     *
     * Flow (blueprint EPOLLOUT path):
     *   send() → consume sent bytes from write_buffer
     *          → if write_buffer empty:
     *              keep-alive → reset() → CS_READING → rearm EPOLLIN
     *              close      → closeConnection()
     *
     * Edge-trigger: loop until EAGAIN or buffer empty.
     *
     * ⚠️  conn may be dangling after close.
     */
    void _handleWrite(Connection* conn)
    {
        const int fd = conn->fd();

        // Edge-trigger write loop — drain until EAGAIN or empty
        while (!conn->writeBuffer().empty())
        {
            ssize_t n = conn->send();

            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;   // kernel buffer full — wait for next EPOLLOUT

                std::cerr << "[EventLoop] send error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _manager->closeConnection(fd);
                return;   // ⚠️ conn dangling
            }
        }

        // ── write_buffer drained? ─────────────────────────
        if (conn->writeBuffer().empty())
        {
            if (conn->request().keepAlive())
            {
                // Keep-alive: reset everything, go back to reading
                conn->setReading();
                _manager->rearmEpoll(fd);
            }
            else
            {
                // No keep-alive: we're done
                _manager->closeConnection(fd);
                // ⚠️ conn dangling
            }
        }
        // else: buffer not empty yet — EPOLLOUT will fire again
    }

    // ── server fd helpers ──────────────────────────────────

    /**
     * True if fd is one of the registered server (listening) fds.
     * O(n) on number of listening sockets — typically 1-5, fine.
     */
    bool _isServerFd(int fd) const
    {
        for (size_t i = 0; i < _server_fds.size(); ++i)
            if (_server_fds[i] == fd)
                return true;
        return false;
    }

    /**
     * Return the ServerConfig* for a server fd.
     * Returns NULL if not found — should never happen in practice.
     */
    const ServerConfig* _configForServer(int fd) const
    {
        for (size_t i = 0; i < _server_fds.size(); ++i)
            if (_server_fds[i] == fd)
                return _server_configs[i];
        return NULL;
    }

    // ── stubs ─────────────────────────────────────────────
    //  Placeholder logic — you replace these with real
    //  HttpParser and ResponseHandler calls in the next phase.

    /**
     * [STUB] Pretend to parse — immediately marks request COMPLETE.
     * Replace with:  _parser.feed(conn->readBuffer(), conn->request());
     */
    void _stubParse(Connection* conn)
    {
        // Real parser reads from conn->readBuffer()
        // and writes into conn->request()
        // For now just mark complete so the loop can be tested
        conn->request().parse_state = PS_COMPLETE;
        conn->request().method      = "GET";
        conn->request().path        = "/";
        conn->request().version     = "HTTP/1.1";
    }

    /**
     * [STUB] Write a bare 400 response into write_buffer.
     * Replace with real ResponseHandler::send400().
     */
    void _stubSend400(Connection* conn)
    {
        const char* r = "HTTP/1.1 400 Bad Request\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
        conn->writeBuffer().append(r, std::strlen(r));
    }

    /**
     * [STUB] Write a bare 200 response into write_buffer.
     * Replace with real ResponseHandler::handle().
     */
    void _stubBuildResponse(Connection* conn)
    {
        const char* r = "HTTP/1.1 200 OK\r\n"
                        "Content-Length: 13\r\n"
                        "Connection: keep-alive\r\n\r\n"
                        "Hello, World!";
        conn->writeBuffer().append(r, std::strlen(r));
    }

    // ── members ────────────────────────────────────────────
    int                          _epoll_fd;       ///< owned by EventLoop
    ConnectionManager*           _manager;        ///< owns all Connections
    bool                         _running;        ///< loop control flag
    std::vector<int>             _server_fds;     ///< listening fds
    std::vector<const ServerConfig*> _server_configs; ///< parallel to _server_fds
};

#endif // EVENT_LOOP_HPP