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
#include "Headers/CgiStarter.hpp"
#include "cgi/CgiHandler.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include <sys/wait.h>
#include <signal.h>

// ── stop ─────────────────────────────────────────────────────
void EventLoop::stop() { _running = false; }

// ── _setCloexec ──────────────────────────────────────────────
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

// ── setNonBlocking ───────────────────────────────────────────
int EventLoop::setNonBlocking(int fd)
{
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
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

    _setCloexec(_epoll_fd, "epoll");

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
    _setCloexec(server_fd, "server");

    _server_fds.push_back(server_fd);
    _server_configs.push_back(config);
    _registerEventFd(server_fd, EV_SERVER, EPOLLIN | EPOLLET);
    std::cerr << "[EventLoop] listening on fd " << server_fd << "\n";
}

// ── addCGI ──────────────────────────────────────────

void EventLoop::_registerEventFd(int fd, EventKind kind, uint32_t events)
{
    EventRef* ref = new EventRef(kind, fd);
    _event_refs[fd] = ref;

    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = ref;

    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error(std::string("epoll_ctl ADD failed: ")
            + std::strerror(errno));
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
        std::cerr << "[EventLoop] epoll_ctl MOD failed for fd " << fd
                  << ": " << std::strerror(errno) << "\n";
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

void EventLoop::_handleClientEvent(int client_fd, uint32_t events)
{
    Connection* conn = _manager->get(client_fd);
    if (!conn)
    {
        std::cerr << "[EventLoop] stale event — connection already gone\n";
        return;
    }

    if (events & (EPOLLERR | EPOLLHUP))
    {
        _handleError(conn);
        return;
    }

    if (events & EPOLLRDHUP)
    {
        if (conn->writeBuffer().empty())
            _closeClient(client_fd);
        else
            conn->setPeerHalfClosed();
        return;
    }

    if (events & EPOLLIN)
    {
        _handleRead(conn);
        return;
    }

    if (events & EPOLLOUT)
    {
        _handleWrite(conn);
        return;
    }
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

void EventLoop::_addCgiFd(int result_fd, int client_fd)
{
    _registerEventFd(result_fd, EV_CGI, EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
    std::cerr << "[EventLoop] CGI fd " << result_fd
              << " registered for client fd " << client_fd << "\n";
}

void EventLoop::_startCgi(Connection* conn, const CgiRequestInfo& info)
{
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) < 0)
    {
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }

    int result_read_fd = fds[0];
    int result_write_fd = fds[1];

    if (EventLoop::setNonBlocking(result_read_fd) < 0)
    {
        ::close(result_read_fd);
        ::close(result_write_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
        return;
    }

    CgiJob* job = new CgiJob(conn->fd(), result_read_fd, conn->writeBuffer().maxSize());
    _cgi_jobs[result_read_fd] = job;
    _addCgiFd(result_read_fd, conn->fd());

    conn->setCgiRunning();
    _rearmClient(conn->fd());

    // bool ok = startCgi(conn->request(), *conn->config(), *info.location,
    //                    info.script_path, result_write_fd);
    CgiHandler cgi(conn->request(), *conn->config(), *info.location, info.script_path);
    bool ok = cgi.startCgi(result_write_fd);
    if (ok)
    {
        job->child_pid = cgi.getChildPid();

        // If the request has a body, take ownership of the (non-blocking)
        // stdin write fd and schedule writes via EPOLLOUT. This avoids the
        // pipe-buffer deadlock for bodies larger than ~64KB.
        int stdin_fd = cgi.releaseStdinFd();
        if (stdin_fd >= 0)
        {
            _setCloexec(stdin_fd, "cgi-stdin");
            job->stdin_fd     = stdin_fd;
            job->stdin_body   = conn->request().body;
            job->stdin_offset = 0;
            _cgi_stdin_jobs[stdin_fd] = job;
            _registerEventFd(stdin_fd, EV_CGI_STDIN,
                             EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP);
        }
    }
    ::close(result_write_fd);

    if (!ok)
    {
        _closeCgiJob(result_read_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }
}

// Close the CGI stdin pipe fd (signals EOF to the child), unregister it
// from epoll, and detach it from the job. Safe to call multiple times.
void EventLoop::_closeCgiStdin(CgiJob* job)
{
    if (!job || job->stdin_fd < 0)
        return;

    int fd = job->stdin_fd;
    _cgi_stdin_jobs.erase(fd);
    _unregisterEventFd(fd);
    ::close(fd);

    job->stdin_fd     = -1;
    job->stdin_offset = 0;
    job->stdin_body.clear();
}

// EPOLLOUT / EPOLLERR / EPOLLHUP on the CGI child's stdin pipe.
// Drain the body buffer incrementally until complete or the pipe blocks.
void EventLoop::_handleCgiStdinEvent(int stdin_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_stdin_jobs.find(stdin_fd);
    if (it == _cgi_stdin_jobs.end())
        return;

    CgiJob* job = it->second;

    // EPOLLERR/EPOLLHUP on the write end means the child closed its stdin
    // (or died). Close our end; the child either has what it needs or is gone.
    if (events & (EPOLLERR | EPOLLHUP))
    {
        _closeCgiStdin(job);
        return;
    }

    while (job->stdin_offset < job->stdin_body.size())
    {
        const char*  data = job->stdin_body.data() + job->stdin_offset;
        const size_t left = job->stdin_body.size() - job->stdin_offset;
        ssize_t n = ::write(stdin_fd, data, left);
        if (n > 0)
        {
            job->stdin_offset += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return;  // pipe full — wait for next EPOLLOUT

        // EPIPE or other error: child closed stdin or died. Give up writing,
        // but keep reading its stdout — it may still have produced output.
        _closeCgiStdin(job);
        return;
    }

    // Body fully written — close write end to signal EOF to the child.
    _closeCgiStdin(job);
}

// Wrapped the read loop in a try/catch block to return a clean 413 instead of crashing.
void EventLoop::_handleCgiEvent(int result_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    CgiJob* job = it->second;

    if (events & (EPOLLERR | EPOLLHUP))
    {
        // still try to drain
    }

    char buf[8192];
    try
    {
        while (true)
        {
            ssize_t n = ::read(result_fd, buf, sizeof(buf));
            if (n > 0)
            {
                job->result_buffer.append(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
            {
                _finishCgiJob(result_fd);
                return;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;

            _failCgiJob(result_fd, 502);
            return;
        }
    }
    catch (const BodyLimitException&)
    {
        _failCgiJob(result_fd, 413);
    }
}
void EventLoop::_finishCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end())
        return;
    CgiJob* job = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.handleCgiOutput(conn->request(),
                                   *conn->config(),
                                   job->result_buffer,
                                   conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }

    _closeCgiJob(result_fd);
}
void EventLoop::_failCgiJob(int result_fd, int status_code)
{
    std::map<int, CgiJob*>::iterator jt = _cgi_jobs.find(result_fd);
    if (jt == _cgi_jobs.end())
        return;
    CgiJob* job = jt->second;
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.sendError(status_code, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _rearmClient(conn->fd());
    }

    _closeCgiJob(result_fd);
}

void EventLoop::_closeCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    // If the stdin writer is still active, close and unregister it first
    // so the child sees EOF and exits promptly.
    _closeCgiStdin(it->second);

    // Reap child process to prevent zombies. Never block the event loop:
    // if the child hasn't exited yet, send SIGKILL and defer the reap.
    if (it->second->child_pid > 0)
    {
        int status;
        pid_t ret = waitpid(it->second->child_pid, &status, WNOHANG);
        if (ret == 0)
        {
            // Child still running — signal it and reap later (non-blocking).
            // A child stuck in uninterruptible sleep (D-state) would otherwise
            // stall the entire event loop if we used waitpid(..., 0) here.
            kill(it->second->child_pid, SIGKILL);
            _pending_reap.push_back(std::make_pair(it->second->child_pid,
                                                     std::time(NULL)));
        }
    }

    _unregisterEventFd(result_fd);
    ::close(result_fd);
    delete it->second;
    _cgi_jobs.erase(it);
}

void EventLoop::_closeCgiJobsForClient(int client_fd)
{
    std::vector<int> to_close;
    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        if (it->second->client_fd == client_fd)
            to_close.push_back(it->first);
    }

    for (size_t i = 0; i < to_close.size(); ++i)
        _closeCgiJob(to_close[i]);
}

// Drain the deferred-reap list non-blockingly. Called every event-loop
// iteration so kills dispatched from _closeCgiJob don't produce zombies
// while never blocking the loop on a stuck child.  Entries older than
// REAP_STALE_SECONDS are dropped to prevent unbounded growth from
// children stuck in uninterruptible sleep (D-state).
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

void EventLoop::_closeTimedOutCgiJobs()
{
    const time_t now = std::time(NULL);
    std::vector<int> timed_out;

    for (std::map<int, CgiJob*>::iterator it = _cgi_jobs.begin();
         it != _cgi_jobs.end(); ++it)
    {
        if (now - it->second->start_time > 10)
            timed_out.push_back(it->first);
    }

    for (size_t i = 0; i < timed_out.size(); ++i)
        _failCgiJob(timed_out[i], 504);
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
            if (errno == EINTR) 
                continue;   // signal interrupted — loop again
            std::cerr << "[EventLoop] epoll_wait error: "
                      << std::strerror(errno) << "\n";
            break;
        }

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

// ── _handleAccept ────────────────────────────────────────────
void EventLoop::_handleAccept(int server_fd)
{
    const ServerConfig* config = _configForServer(server_fd);

    while (true)
    {
        int client_fd = _manager->addConnection(server_fd, config);
        if (client_fd < 0)
            break;
        _setCloexec(client_fd, "client");
        _registerEventFd(client_fd, EV_CLIENT, EPOLLIN | EPOLLET | EPOLLRDHUP);
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
    _closeClient(fd);
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
                _closeClient(fd);
                return;
            }

            if (n < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;  // edge-trigger drained
                std::cerr << "[EventLoop] recv error on fd " << fd
                          << ": " << std::strerror(errno) << "\n";
                _closeClient(fd);
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
                _rearmClient(fd);
                return;  // wait for EPOLLOUT to drain the error response
            }
/*this is where i am gonna add cgi call */
            if (conn->request().parse_state == PSTATE_COMPLETE)
            {
                conn->setProcessing();

                CgiRequestInfo cgi;
                if (_responder.resolveCgiRequest(conn->request(), *conn->config(), cgi))
                {
                    _startCgi(conn, cgi);
                    return;
                }

                _responder.handle(conn->request(),
                                *conn->config(),
                                conn->writeBuffer());

                conn->setWriting();
                _rearmClient(fd);
                return;
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
        _rearmClient(fd);
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
            _closeClient(fd);
            return;
        }
    }

    if (conn->writeBuffer().empty())
    {
        if (!conn->peerHalfClosed() && conn->request().keepAlive())
        {
            conn->setReading();        // resets buffers + request + stamps time
            _rearmClient(fd);  // re-arm EPOLLIN
        }
        else
        {
            _closeClient(fd);
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
