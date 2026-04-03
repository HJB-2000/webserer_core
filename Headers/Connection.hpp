// ============================================================
//  Connection.hpp
//  Represents one live client connection.
//  C++98 compliant.
//
//  Ownership model:
//  ─────────────────────────────────────
//  std::map<int, Connection*> _connections  owns every Connection
//  epoll_event.data.ptr                     borrows the raw pointer
//
//  ⚠️  DANGER:
//      1. delete  the Connection BEFORE erasing from the map
//      2. NEVER   dereference epoll's data.ptr after closeConnection()
//      3. Always  go through the map to reach a Connection
//
//  Non-trivial method implementations live in Connection.cpp.
//  Trivial accessors and one-liner state transitions are inline here.
//
//  Dependency chain:
//    Buffer.hpp
//    HttpRequest.hpp       → parse_state, keepAlive()
//    ConnectionState.hpp   → CS_READING / PROCESSING / WRITING / CLOSING
//        └── Connection.hpp   ← we are here
// ============================================================
#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <sys/epoll.h>   // epoll_event
#include <sys/socket.h>  // send() recv()
#include <unistd.h>      // close()
#include <ctime>         // time_t, time()
#include <cerrno>        // errno

#include "buffer.hpp"
#include "HttpRequest.hpp"
#include "ConnectionState.hpp"

// ── Forward declarations ────────────────────────────────────
class ServerConfig;     // provided by teammate (Phase 1)
class HttpParser;       // Phase 2 — plugs into readBuffer() + request()
class ResponseHandler;  // Phase 3 — plugs into request() + writeBuffer()


// ────────────────────────────────────────────────────────────
//  Connection
//
//  One instance = one live TCP client connection.
//  Created by ConnectionManager::addConnection().
//  Destroyed by ConnectionManager::closeConnection().
// ────────────────────────────────────────────────────────────
class Connection
{
public:

    // ── ctor / dtor ───────────────────────────────────────
    // Implemented in Connection.cpp.
    // ctor initialises both Buffers with config->client_max_body_size.
    Connection(int fd, const ServerConfig* config);
    ~Connection();

    // ── I/O ───────────────────────────────────────────────
    // Both implemented in Connection.cpp.

    /**
     * Read available bytes into readBuffer().
     * Uses a 16 KB stack buffer as the intermediary so Buffer
     * controls its own growth.
     *
     * @return  n > 0  bytes read and appended to readBuffer()
     *          0      peer closed the connection cleanly
     *         -1      EAGAIN/EWOULDBLOCK (edge-trigger: drained)
     *                 or real recv() error (errno set)
     *
     * May throw BufferOverflowException → caller maps to 413.
     */
    ssize_t recv();

    /**
     * Send as many bytes from writeBuffer() as the kernel accepts.
     * Consumes sent bytes from writeBuffer() via Buffer::consume().
     *
     * @return  n >= 0  bytes sent
     *         -1       EAGAIN/EWOULDBLOCK or real send() error
     */
    ssize_t send();

    // ── keep-alive reset ──────────────────────────────────
    /**
     * Wipe read/write buffers and HttpRequest for the next request
     * on a keep-alive connection.  Stamps last_active and sets
     * state → CS_READING.
     * Implemented in Connection.cpp.
     */
    void reset();

    // ── epoll integration ─────────────────────────────────
    /**
     * Build an epoll_event for epoll_ctl().
     * Sets data.ptr = this (ConnectionManager's map still owns us).
     * Implemented in Connection.cpp.
     */
    epoll_event buildEpollEvent();

    // ── timeout ───────────────────────────────────────────
    /** True when the connection has been idle > timeout_seconds. */
    bool isTimedOut(time_t timeout_seconds) const
    {
        return (std::time(NULL) - _last_active) > timeout_seconds;
    }

    // ── accessors (all inline — trivial) ──────────────────
    int                  fd()          const { return _fd;           }
    ConnectionState      state()       const { return _state;        }
    time_t               lastActive()  const { return _last_active;  }
    const ServerConfig*  config()      const { return _config;       }

    Buffer&       readBuffer()        { return _read_buffer;  }
    const Buffer& readBuffer()  const { return _read_buffer;  }
    Buffer&       writeBuffer()       { return _write_buffer; }
    const Buffer& writeBuffer() const { return _write_buffer; }

    HttpRequest&       request()       { return _request; }
    const HttpRequest& request() const { return _request; }

    // ── state transitions (all inline — one-liners) ───────
    /** READING → PROCESSING : full request arrived. */
    void setProcessing() { _state = CS_PROCESSING; }

    /** PROCESSING → WRITING : response is in writeBuffer(). Caller re-arms epoll. */
    void setWriting()    { _state = CS_WRITING; }

    /** Any → CLOSING : error / HUP / timeout / clean shutdown. */
    void setClosing()    { _state = CS_CLOSING; }

    /**
     * WRITING → READING : keep-alive, writeBuffer is empty.
     * Calls reset() which wipes buffers, request, stamps time,
     * and sets state → CS_READING.
     */
    void setReading()
    {
        reset();
        _state = CS_READING;  // explicit — reset() also sets this, belt-and-suspenders
    }

private:

    // ── non-copyable ──────────────────────────────────────
    Connection(const Connection&);
    Connection& operator=(const Connection&);

    // ── helpers ───────────────────────────────────────────
    void _touchActive() { _last_active = std::time(NULL); }

    // ── members ───────────────────────────────────────────
    int                 _fd;            ///< file descriptor — closed only in ~Connection()
    const ServerConfig* _config;        ///< borrowed pointer — never owned or deleted
    Buffer              _read_buffer;   ///< inbound raw bytes
    Buffer              _write_buffer;  ///< outbound response bytes
    HttpRequest         _request;       ///< Parser fills this; Processing reads it
    ConnectionState     _state;         ///< drives epoll interest and dispatch
    time_t              _last_active;   ///< updated by every successful I/O
};

#endif // CONNECTION_HPP
