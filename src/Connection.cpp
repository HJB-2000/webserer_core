// ============================================================
//  Connection.cpp
//  Implements the non-trivial methods of Connection.
//
//  Kept separate from the header so the interface stays clean
//  and the server core can be compiled as a discrete unit.
//
//  ServerConfig.hpp must be included here (not just forward-
//  declared) because the constructor accesses config members.
// ============================================================

#include "serverConfig.hpp"
#include "Headers/Connection.hpp"

#include <cstring>   // memset (for epoll_event)
#include <iostream>  // std::cerr (debug logging)

// ── Constructor ─────────────────────────────────────────────
//
// Both Buffers are initialised with config->client_max_body_size
// so the 413 ceiling is enforced from the very first byte.
// last_active is stamped at construction so timeout tracking begins
// the moment the connection is accepted, not on first data.
Connection::Connection(int fd, const ServerConfig* config)
    : _fd(fd)
    , _config(config)
    , _read_buffer(config->getMaxBody())
    , _write_buffer(config->getMaxBody())
    , _request()
    , _state(CSTATE_READING)
    , _last_active(std::time(NULL))
    , _peer_half_closed(false)
{}

// ── Destructor ───────────────────────────────────────────────
//
// Single close point for the fd.
// ConnectionManager always calls:
//   delete connections[fd];    ← destructor fires here
//   connections.erase(fd);
// so close() happens exactly once.
Connection::~Connection()
{
    if (_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
    }
}

// ── recv ─────────────────────────────────────────────────────
//
// Reads from the kernel socket buffer into a 16 KB stack buffer,
// then appends to readBuffer() (the Buffer object).
// Using a stack buffer keeps Buffer in full control of its memory.
//
// In edge-trigger mode the caller loops this until EAGAIN.
// BufferOverflowException propagates up — the EventLoop / core
// catches it and sends a 413.
ssize_t Connection::recv()
{
    char    tmp[16 * 1024];
    ssize_t n = ::recv(_fd, tmp, sizeof(tmp), 0);

    if (n > 0)
    {
        // May throw BufferOverflowException → caller catches → 413
        _read_buffer.append(tmp, static_cast<size_t>(n));
        _touchActive();
    }
    return n;
}

// ── send ─────────────────────────────────────────────────────
//
// Drains as many bytes as the kernel will accept from writeBuffer().
// Buffer::consume() tracks the offset internally — no manual index.
// MSG_NOSIGNAL prevents SIGPIPE if the peer has already closed.
ssize_t Connection::send()
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

// ── reset ────────────────────────────────────────────────────
//
// Full wipe for keep-alive reuse.
// Clears raw byte buffers, the parsed request, resets state to
// CSTATE_READING, and stamps last_active so timeout restarts cleanly.
// Called by setReading() and (rarely) directly for error recovery.
void Connection::reset()
{
    _read_buffer.reset();
    _write_buffer.reset();
    _request.reset();
    _state             = CSTATE_READING;
    _peer_half_closed  = false;
    _touchActive();
}

// ── isTimedOut ───────────────────────────────────────────────
bool Connection::isTimedOut(time_t timeout_seconds) const
{
    return (std::time(NULL) - _last_active) > timeout_seconds;
}

// ── accessors ────────────────────────────────────────────────
int                 Connection::fd()          const { return _fd;           }
ConnectionState     Connection::state()       const { return _state;        }
time_t              Connection::lastActive()  const { return _last_active;  }
const ServerConfig* Connection::config()      const { return _config;       }

Buffer&       Connection::readBuffer()        { return _read_buffer;  }
const Buffer& Connection::readBuffer()  const { return _read_buffer;  }
Buffer&       Connection::writeBuffer()       { return _write_buffer; }
const Buffer& Connection::writeBuffer() const { return _write_buffer; }

HttpRequest&       Connection::request()       { return _request; }
const HttpRequest& Connection::request() const { return _request; }

// ── state transitions ────────────────────────────────────────
void Connection::setProcessing() { _state = CSTATE_PROCESSING; }
void Connection::setWriting()    { _state = CSTATE_WRITING;    }
void Connection::setClosing()    { _state = CSTATE_CLOSING;    }
void Connection::setCgiRunning() {_state = CSTATE_CGI_RUNNING;}
void Connection::setReading()
{
    reset();
    _state = CSTATE_READING;
}


// ── peer half-close ──────────────────────────────────────────
void Connection::setPeerHalfClosed() { _peer_half_closed = true; }
bool Connection::peerHalfClosed() const { return _peer_half_closed; }

// ── private helpers ──────────────────────────────────────────
void Connection::_touchActive() { _last_active = std::time(NULL); }

// ── buildEpollEvent ──────────────────────────────────────────
//
// Returns an epoll_event ready for epoll_ctl().
//
// Interest mask is derived from current state:
//   CSTATE_READING    → EPOLLIN  | EPOLLET | EPOLLRDHUP
//   CSTATE_WRITING    → EPOLLOUT | EPOLLET | EPOLLRDHUP
//   CS_PROCESSING → EPOLLET  | EPOLLRDHUP  (no I/O interest)
//   CS_CLOSING    → EPOLLET  | EPOLLRDHUP  (no I/O interest)
//
// data.ptr = this  so the event loop can recover the Connection*
// directly without a map lookup.
// ⚠️  After closeConnection() this pointer is dangling.
//     The dispatch loop MUST NOT dereference it after deletion.
epoll_event Connection::buildEpollEvent()
{
    epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.data.ptr = this;   // borrow — ConnectionManager's map owns the memory
    ev.events   = EPOLLET | EPOLLRDHUP;

    switch (_state)
    {
        case CSTATE_READING:
            ev.events |= EPOLLIN;
            break;
        case CSTATE_WRITING:
            ev.events |= EPOLLOUT;
            break;
        default:
            break;  // CS_PROCESSING / CS_CLOSING / CS_CGI_RUNNING: no I/O interest
    }
    return ev;
}

void Connection::updateBufferSizes(size_t new_max)  
{  
    _read_buffer.setMaxSize(new_max);  
    _write_buffer.setMaxSize(new_max);  
}