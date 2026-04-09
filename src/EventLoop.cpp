// ============================================================
//  EventLoop.cpp — implementations
//
//  All non-trivial EventLoop methods live here.
//  The header (EventLoop.hpp) is declarations only.
// ============================================================

#include "serverConfig.hpp"
#include "Headers/HttpRequest.hpp"
#include "Headers/EventLoop.hpp"
#include "Headers/HttpParser.hpp"
#include "Headers/ResponseHandler.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>

// ── stop ─────────────────────────────────────────────────────
void EventLoop::stop() { _running = false; }

// ── setNonBlocking ───────────────────────────────────────────
int EventLoop::setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ── Constructor ──────────────────────────────────────────────
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

    _manager = new ConnectionManager(_epoll_fd);
}

// ── Destructor ───────────────────────────────────────────────
EventLoop::~EventLoop()
{
    delete _manager;
    if (_epoll_fd >= 0)
        ::close(_epoll_fd);
}

// ── addServerSocket ──────────────────────────────────────────
//
// Register a bound, listening, non-blocking fd with epoll.
// Server sockets use data.fd (not data.ptr) — they have no Connection*.
// ⚠️  Caller must set the fd non-blocking before calling this.
void EventLoop::addServerSocket(int server_fd, const ServerConfig* config)
{
    _server_fds.push_back(server_fd);
    _server_configs.push_back(config);

    epoll_event ev;
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd;

    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0)
        throw std::runtime_error(
            std::string("[EventLoop] epoll_ctl ADD server fd failed: ")
            + std::strerror(errno));

    std::cerr << "[EventLoop] listening on fd " << server_fd << "\n";
}

// ── run ──────────────────────────────────────────────────────
//
// Blocking event loop.  Each iteration:
//   1. epoll_wait → fills event batch
//   2. dispatch   → one handler per event
//   3. sweep      → close idle connections (per-connection timeout)
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
            if (errno == EINTR) continue;   // signal interrupted — loop again
            std::cerr << "[EventLoop] epoll_wait error: "
                      << std::strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < n; ++i)
            _dispatch(events[i]);

        // Per-connection timeout: uses each conn's config->timeout_seconds.
        _manager->closeTimedOut();
    }

    std::cerr << "[EventLoop] stopped\n";
}

// ── _dispatch ────────────────────────────────────────────────
//
// Route a single epoll_event to the correct handler.
//
// Server fds → registered with data.fd  → _handleAccept
// Client fds → registered with data.ptr → _handleRead / _handleWrite
//
// ⚠️  Never use conn after any call that may closeConnection().
//     Each handler returns immediately after a close.
void EventLoop::_dispatch(const epoll_event& ev)
{
    // ── server fd ─────────────────────────────────────────
    if (_isServerFd(ev.data.fd))
    {
        _handleAccept(ev.data.fd);
        return;
    }

    // ── client fd ─────────────────────────────────────────
    // Recover the Connection* that buildEpollEvent() stored in data.ptr.
    Connection* conn = static_cast<Connection*>(ev.data.ptr);

    // Safety check: the connection may have been closed earlier in the
    // same epoll_wait batch (two events for the same fd in one tick).
    if (!conn || !_manager->get(conn->fd()))
    {
        std::cerr << "[EventLoop] stale event — connection already gone\n";
        return;
    }

    if (ev.events & (EPOLLERR | EPOLLHUP))
    {
        _handleError(conn);
        return;  // conn is dangling
    }

    if (ev.events & EPOLLRDHUP)
    {
        // Peer shut down their write side (FIN received).
        // If we still have data to send, let _handleWrite drain it first,
        // then close. Otherwise close now.
        if (conn->writeBuffer().empty())
            _manager->closeConnection(conn->fd());
        else
            conn->setPeerHalfClosed();
        return;
    }

    if (ev.events & EPOLLIN)
    {
        _handleRead(conn);
        return;  // conn may be dangling
    }

    if (ev.events & EPOLLOUT)
    {
        _handleWrite(conn);
        return;  // conn may be dangling
    }
}

// ── _handleAccept ────────────────────────────────────────────
//
// Drain all pending clients on a server fd.
// ConnectionManager::addConnection() does the full creation sequence:
//   accept → setNonBlocking → new Connection → map insert → epoll ADD
void EventLoop::_handleAccept(int server_fd)
{
    const ServerConfig* config = _configForServer(server_fd);

    while (true)
    {
        int client_fd = _manager->addConnection(server_fd, config);
        if (client_fd < 0)
            break;  // EAGAIN — edge-trigger drained
    }
}

// ── _handleError ─────────────────────────────────────────────
//
// EPOLLERR or EPOLLHUP on a client fd — close immediately.
// ⚠️  conn is dangling after return.
void EventLoop::_handleError(Connection* conn)
{
    const int fd = conn->fd();
    std::cerr << "[EventLoop] error/hup on fd " << fd << "\n";
    _manager->closeConnection(fd);
}

// ── _handleRead ──────────────────────────────────────────────
//
// EPOLLIN on a client fd.
//
// Flow:
//   recv() → append to readBuffer()
//   stub parse → fills HttpRequest
//   PS_ERROR    → queue 400 → setWriting → rearmEpoll → (send → close)
//   PS_COMPLETE → queue response → setWriting → rearmEpoll EPOLLOUT
//   partial     → keep reading
//   recv == 0   → peer closed cleanly → closeConnection
//
// Edge-trigger: loop recv() until EAGAIN.
// BodyLimitException → queue 413 → same write path.
//
// ⚠️  conn may be dangling after close. Always return after close.
void EventLoop::_handleRead(Connection* conn)
{
    const int fd = conn->fd();

    try
    {
        while (true)
        {
            ssize_t n = conn->recv();

            if (n == 0)
            {
                _manager->closeConnection(fd);
                return;
            }

            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;  // edge-trigger drained
                std::cerr << "[EventLoop] recv error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _manager->closeConnection(fd);
                return;
            }

            // ── Phase 2: HttpParser ──────────────────────────
            _parser.feed(conn->readBuffer(), conn->request());
            // ─────────────────────────────────────────────────

            if (conn->request().parse_state == PSTATE_ERROR)
            {
                conn->request().headers["connection"] = "close";
                std::cerr << "[EventLoop] parse error " << conn->request().error_code
                          << " on fd " << fd << "\n";
                // Phase 3: real error response
                _responder.sendError(conn->request().error_code,
                                     *conn->config(),
                                     conn->writeBuffer());
                conn->setWriting();
                _manager->rearmEpoll(fd);
                return;  // wait for EPOLLOUT to drain the error response
            }

            if (conn->request().parse_state == PSTATE_COMPLETE)
            {
                conn->setProcessing();

                // Phase 3: build the full HTTP response
                _responder.handle(conn->request(),
                                  *conn->config(),
                                  conn->writeBuffer());

                conn->setWriting();
                _manager->rearmEpoll(fd);
                return;  // wait for EPOLLOUT — stop reading
            }
            // PS_IDLE / PS_HEADERS / PS_BODY → partial, keep reading
        }
    }
    catch (const BodyLimitException&)
    {
        // readBuffer exceeded client_max_body_size → 413
        conn->request().headers["connection"] = "close";
        // Phase 3: real 413 response
        _responder.sendError(413, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _manager->rearmEpoll(fd);
    }
}

// ── _handleWrite ─────────────────────────────────────────────
//
// EPOLLOUT on a client fd.
//
// Drain writeBuffer until EAGAIN or empty.
// When empty:
//   keepAlive() → setReading() → rearmEpoll EPOLLIN
//   !keepAlive() → closeConnection()
//
// keepAlive() is read BEFORE setReading() which resets the request.
// ⚠️  conn may be dangling after close.
void EventLoop::_handleWrite(Connection* conn)
{
    const int fd = conn->fd();

    while (!conn->writeBuffer().empty())
    {
        ssize_t n = conn->send();
        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;  // kernel buffer full — wait for next EPOLLOUT
            std::cerr << "[EventLoop] send error on fd " << fd
                      << ": " << std::strerror(errno) << "\n";
            _manager->closeConnection(fd);
            return;
        }
    }

    if (conn->writeBuffer().empty())
    {
        if (!conn->peerHalfClosed() && conn->request().keepAlive())
        {
            conn->setReading();        // resets buffers + request + stamps time
            _manager->rearmEpoll(fd);  // re-arm EPOLLIN
        }
        else
        {
            _manager->closeConnection(fd);
            // ⚠️ conn dangling
        }
    }
    // else: buffer not empty — EPOLLOUT will fire again
}

// ── helpers ──────────────────────────────────────────────────

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

// Phase 3 integrated — stubs removed.
// Response building is now handled by _responder (ResponseHandler).
