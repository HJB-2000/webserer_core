// ============================================================
//  Connection.hpp
//  Represents one live client connection.
//  C++98 compliant.
//
//  Implementation: src/Connection.cpp
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
//  Dependency chain:
//    Buffer.hpp
//    HttpRequest.hpp       → parse_state, keepAlive()
//    ConnectionState.hpp   → CSTATE_READING / PROCESSING / WRITING / CLOSING
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

#include "serverConfig.hpp"  // Server + typedef ServerConfig (Phase 1)
class HttpParser;       // Phase 2
class ResponseHandler;  // Phase 3


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
    Connection(int fd, const ServerConfig* config);
    ~Connection();

    // ── I/O ───────────────────────────────────────────────
    ssize_t recv();
    ssize_t send();

    // ── keep-alive reset ──────────────────────────────────
    void reset();

    // ── epoll integration ─────────────────────────────────
    epoll_event buildEpollEvent();

    // ── timeout ───────────────────────────────────────────
    bool isTimedOut(time_t timeout_seconds) const;

    // ── accessors ─────────────────────────────────────────
    int                  fd()          const;
    ConnectionState      state()       const;
    time_t               lastActive()  const;
    const ServerConfig*  config()      const;
    
    Buffer&       readBuffer();
    const Buffer& readBuffer()  const;
    Buffer&       writeBuffer();
    const Buffer& writeBuffer() const;
    
    HttpRequest&       request();
    const HttpRequest& request() const;
    
    // ── state transitions ─────────────────────────────────
    void setProcessing();   ///< READING → PROCESSING
    void setWriting();      ///< PROCESSING → WRITING
    void setClosing();      ///< Any → CLOSING
    void setReading();      ///< WRITING → READING (calls reset())
    void setCgiRunning();

    // ── peer half-close tracking ──────────────────────────
    // Set when EPOLLRDHUP fires while the write buffer is still
    // non-empty. EventLoop::_handleWrite() closes the connection
    // once the buffer fully drains.
    void setPeerHalfClosed();
    bool peerHalfClosed() const;
    void updateBufferSizes(size_t new_max);

    // getter and setter for the ip_client
    const std::string& get_clientIp() const { return _client_ip; }
    void setClientIp(const std::string& ip) { _client_ip = ip; }
private:

    // ── non-copyable ──────────────────────────────────────
    Connection(const Connection&);
    Connection& operator=(const Connection&);

    // ── helpers ───────────────────────────────────────────
    void _touchActive();

    // ── members ───────────────────────────────────────────
    int                 _fd;
    const ServerConfig* _config;
    Buffer              _read_buffer;
    Buffer              _write_buffer;
    HttpRequest         _request;
    ConnectionState     _state;
    time_t              _last_active;
    bool                _peer_half_closed;
    std::string         _client_ip;

};

#endif // CONNECTION_HPP
