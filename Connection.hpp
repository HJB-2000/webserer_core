// ============================================================
//  Connection.hpp
//  Represents one live client connection.
//  C++98 compliant — lives entirely in this header.
//
//  Ownership model (from the blueprint):
//  ─────────────────────────────────────
//  std::map<int, Connection*> connections  owns every Connection
//  epoll_event.data.ptr                    borrows the raw pointer
//
//  ⚠️  DANGER — enforced by convention throughout the server:
//      1. delete  the Connection BEFORE erasing from the map
//      2. NEVER   dereference epoll's data.ptr after closeConnection()
//      3. Always  go through the map to reach a Connection
//
//  CREATE  path:  new Connection(fd, config) → insert into map
//                 → arm epoll with EPOLLIN | EPOLLET
//                 → store &conn in epoll_event.data.ptr
//
//  DESTROY path:  closeConnection(fd)
//                   → delete connections[fd]   (1st)
//                   → connections.erase(fd)    (2nd)
//
//  Connection between previous headers:
//  ─────────────────────────────────────
//  Buffer          (Buffer.hpp)          → read_buffer, write_buffer
//  HttpRequest     (HttpRequest.hpp)     → request
//  ParseState      (HttpRequest.hpp)     → checked after every read
//  ConnectionState (ConnectionState.hpp) → state machine driver
// ============================================================
#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <sys/epoll.h>   // epoll_event
#include <sys/socket.h>  // send() recv()
#include <unistd.h>      // close()
#include <ctime>         // time_t, time()
#include <cerrno>        // errno
#include <stdexcept>     // std::runtime_error

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "ConnectionState.hpp"

// ── Forward declarations ────────────────────────────────────
//  Full definitions come from their own headers (not written yet).
//  Connection only holds pointers / references to these so a
//  forward declaration is enough here.
class ServerConfig;   // owns allowed_methods, cmbs, locations …
class HttpParser;     // fills HttpRequest from read_buffer
class ResponseHandler;// builds response into write_buffer


// ────────────────────────────────────────────────────────────
//  Connection
//
//  One instance = one live TCP client connection.
//  Created by the server's accept() branch inside epoll_wait().
//  Destroyed by closeConnection() — never anywhere else.
// ────────────────────────────────────────────────────────────
class Connection
{
public:

    // ── ctor ───────────────────────────────────────────────
    /**
     * Construct a fresh Connection for an accepted fd.
     *
     * @param fd      The file descriptor returned by accept().
     * @param config  Borrowed pointer to the matching ServerConfig.
     *                Connection never owns this — ServerConfig
     *                outlives every Connection.
     *
     * read_buffer and write_buffer are initialised with the
     * client_max_body_size from config so their growth policy
     * is correct from the very first byte.
     *
     * last_active is stamped immediately so timeout tracking
     * starts from the moment of connection, not first data.
     */
    Connection(int fd, const ServerConfig* config)
        : _fd(fd)
        , _config(config)
        , _read_buffer(config->client_max_body_size)
        , _write_buffer(config->client_max_body_size)
        , _request()
        , _state(CS_READING)
        , _last_active(std::time(NULL))
    {}

    // ── dtor ───────────────────────────────────────────────
    /**
     * Close the file descriptor.
     * Called automatically when the server does:
     *   delete connections[fd];
     *   connections.erase(fd);
     *
     * ⚠️  Never call close() on _fd anywhere else.
     *     The destructor is the single point of truth.
     */
    ~Connection()
    {
        if (_fd >= 0)
        {
            ::close(_fd);
            _fd = -1;
        }
    }

    // ── I/O : read side ────────────────────────────────────
    /**
     * Called when epoll fires EPOLLIN on this fd.
     * Reads available bytes into read_buffer.
     * Updates last_active on every successful read.
     *
     * @return  Number of bytes read.
     *          0  → peer closed connection cleanly → caller sets CS_CLOSING
     *         -1  → EAGAIN/EWOULDBLOCK (edge-trigger, done for now)
     *              or real error         → caller sets CS_CLOSING
     *
     * The caller (epoll loop) is responsible for:
     *   1. Passing the buffer content to the Parser
     *   2. Checking request.parse_state for PS_COMPLETE / PS_ERROR
     *   3. Transitioning state to CS_PROCESSING or CS_CLOSING
     */
    ssize_t recv()
    {
        // Temporary stack buffer — we read into it then append
        // to read_buffer so Buffer controls its own memory.
        char   tmp[4096];
        ssize_t n = ::recv(_fd, tmp, sizeof(tmp), 0);

        if (n > 0)
        {
            // May throw BufferOverflowException → caller catches → 413
            _read_buffer.append(tmp, static_cast<size_t>(n));
            _touchActive();
        }
        return n;
    }

    /**
     * Called when epoll fires EPOLLOUT on this fd.
     * Drains as much of write_buffer as the kernel will accept.
     * Updates last_active on every successful send.
     *
     * @return  Bytes sent  (>= 0)
     *         -1 on real error → caller sets CS_CLOSING
     *
     * After this call the caller checks:
     *   if write_buffer.empty() && keep-alive → reset() → CS_READING
     *   if write_buffer.empty() && !keep-alive → CS_CLOSING
     */
    ssize_t send()
    {
        if (_write_buffer.empty())
            return 0;

        ssize_t n = ::send(_fd,
                           _write_buffer.data(),
                           _write_buffer.size(),
                           MSG_NOSIGNAL);
        if (n > 0)
        {
            _write_buffer.consume(static_cast<size_t>(n));
            _touchActive();
        }
        return n;
    }

    // ── keep-alive reset ───────────────────────────────────
    /**
     * Reset the Connection for the next request on a keep-alive
     * connection.
     *
     * Clears:
     *   - read_buffer   (raw bytes from previous request)
     *   - write_buffer  (should already be empty, safety clear)
     *   - request       (all parsed fields wiped)
     *   - state         → CS_READING
     *   - last_active   → now
     *
     * epoll stays armed with EPOLLIN — no re-registration needed
     * because we never changed the fd, only the state.
     */
    void reset()
    {
        _read_buffer.reset();
        _write_buffer.reset();
        _request.reset();
        _state       = CS_READING;
        _touchActive();
    }

    // ── timeout check ──────────────────────────────────────
    /**
     * Returns true if this connection has been idle longer than
     * the given timeout in seconds.
     *
     * Called by the server's periodic sweep (e.g. every loop
     * iteration or on a timer).  If true, caller calls
     * closeConnection(fd).
     *
     * @param timeout_seconds  From ServerConfig (e.g. 60).
     */
    bool isTimedOut(time_t timeout_seconds) const
    {
        return (std::time(NULL) - _last_active) > timeout_seconds;
    }

    // ── epoll integration ──────────────────────────────────
    /**
     * Build an epoll_event ready to be passed to epoll_ctl().
     *
     * events is set based on current state:
     *   CS_READING    → EPOLLIN  | EPOLLET
     *   CS_WRITING    → EPOLLOUT | EPOLLET
     *   CS_PROCESSING → 0  (no event, server handles inline)
     *   CS_CLOSING    → 0  (no event, about to be removed)
     *
     * data.ptr = this  ← epoll borrows our raw pointer.
     *                    The map still owns us.
     *
     * ⚠️  After closeConnection() this pointer is dangling.
     *     The epoll loop MUST NOT dereference it after deletion.
     *
     * Usage:
     *   epoll_event ev = conn.buildEpollEvent();
     *   epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn.fd(), &ev);
     */
    epoll_event buildEpollEvent()
    {
        epoll_event ev;
        ev.data.ptr = this;   // borrow — map owns the memory
        ev.events   = 0;

        switch (_state)
        {
            case CS_READING:
                ev.events = EPOLLIN  | EPOLLET | EPOLLRDHUP;
                break;
            case CS_WRITING:
                ev.events = EPOLLOUT | EPOLLET | EPOLLRDHUP;
                break;
            case CS_PROCESSING:
            case CS_CLOSING:
                ev.events = EPOLLET | EPOLLRDHUP;
                break;
        }
        return ev;
    }

    // ── accessors ──────────────────────────────────────────
    //  All return references or values — no copies of heavy objects.

    /** The file descriptor. Unique key in the connections map. */
    int fd() const { return _fd; }

    /** Current state machine position. */
    ConnectionState state()  const { return _state; }

    /** Timestamp of last successful I/O. */
    time_t          lastActive() const { return _last_active; }

    /** The ServerConfig this connection matched on accept(). */
    const ServerConfig* config() const { return _config; }

    /** Raw bytes received but not yet fully parsed. */
    Buffer&       readBuffer()  { return _read_buffer;  }
    const Buffer& readBuffer()  const { return _read_buffer;  }

    /** Response bytes waiting to be sent. */
    Buffer&       writeBuffer() { return _write_buffer; }
    const Buffer& writeBuffer() const { return _write_buffer; }

    /** The request object Parser writes into. */
    HttpRequest&       request() { return _request; }
    const HttpRequest& request() const { return _request; }

    // ── state transitions ──────────────────────────────────
    //  Only these functions may change _state.
    //  Keeping transitions explicit makes the flow auditable.

    /** READING → PROCESSING : full request received and parsed. */
    void setProcessing()
    {
        _state = CS_PROCESSING;
    }

    /**
     * PROCESSING → WRITING : response is in write_buffer.
     * Caller must re-arm epoll with EPOLLOUT after this.
     */
    void setWriting()
    {
        _state = CS_WRITING;
    }

    /**
     * Any → CLOSING : error, HUP, timeout, or clean shutdown.
     * Caller calls closeConnection(fd) immediately after.
     */
    void setClosing()
    {
        _state = CS_CLOSING;
    }

    /**
     * WRITING → READING : keep-alive, write_buffer is empty.
     * Calls reset() internally — single call, clean slate.
     */
    void setReading()
    {
        reset();            // wipes buffers + request + stamps time
        _state = CS_READING;// reset() already sets this but be explicit
    }

private:

    // ── non-copyable ───────────────────────────────────────
    //  A Connection owns a real fd. Copying would double-close it.
    Connection(const Connection&);
    Connection& operator=(const Connection&);

    // ── helpers ────────────────────────────────────────────
    /** Stamp last_active = now. Called on every successful I/O. */
    void _touchActive() { _last_active = std::time(NULL); }

    // ── members (matches blueprint exactly) ────────────────
    int                  _fd;            ///< file descriptor
    const ServerConfig*  _config;        ///< borrowed — never owned
    Buffer               _read_buffer;   ///< raw inbound bytes
    Buffer               _write_buffer;  ///< raw outbound bytes
    HttpRequest          _request;       ///< parsed request data
    ConnectionState      _state;         ///< state machine position
    time_t               _last_active;   ///< last successful I/O stamp
};

#endif // CONNECTION_HPP