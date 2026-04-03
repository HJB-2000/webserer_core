// ============================================================
//  EventLoop.hpp — declarations only
//
//  The core epoll dispatch loop.  C++98 compliant.
//
//  Integration seams (in EventLoop.cpp — replace stubs when ready):
//    _stubParse()         → HttpParser::feed()
//    _stubBuildResponse() → ResponseHandler::handle()
//    _stubSend400()       → ResponseHandler::sendError(400, ...)
//    _stub413()           → ResponseHandler::sendError(413, ...)
//
//  Dependency chain:
//    Buffer → HttpRequest → ConnectionState → Connection
//        → ConnectionManager → EventLoop   ← we are here
// ============================================================
#ifndef EVENT_LOOP_HPP
#define EVENT_LOOP_HPP

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

#include "ConnectionManager.hpp"
#include "Connection.hpp"
#include "HttpParser.hpp"

class ServerConfig;
class ResponseHandler;  // Phase 3


class EventLoop
{
public:

    static const int MAX_EVENTS       = 64;    ///< epoll batch size
    static const int EPOLL_TIMEOUT_MS = 1000;  ///< epoll_wait ceiling (ms)

    // Implemented in EventLoop.cpp
    EventLoop();
    ~EventLoop();

    void addServerSocket(int server_fd, const ServerConfig* config);
    void run();

    void stop();
    static int setNonBlocking(int fd);

    ConnectionManager& manager() { return *_manager; }

private:

    // ── non-copyable ───────────────────────────────────────
    EventLoop(const EventLoop&);
    EventLoop& operator=(const EventLoop&);

    // ── private methods (implemented in EventLoop.cpp) ─────
    void _dispatch(const epoll_event& ev);
    void _handleAccept(int server_fd);
    void _handleError(Connection* conn);
    void _handleRead(Connection* conn);
    void _handleWrite(Connection* conn);

    bool                _isServerFd(int fd) const;
    const ServerConfig* _configForServer(int fd) const;

    // Phase 2 done — _stubParse() removed.
    // Phase 3 stubs — replaced when ResponseHandler is integrated:
    void _stubBuildResponse(Connection* conn);
    void _stubSend400(Connection* conn);
    void _stub413(Connection* conn);

    // ── members ────────────────────────────────────────────
    int                              _epoll_fd;
    ConnectionManager*               _manager;
    bool                             _running;
    std::vector<int>                 _server_fds;
    std::vector<const ServerConfig*> _server_configs;

    // Phase 2: one stateless parser serves all connections.
    // All parse progress lives in HttpRequest, so no per-connection
    // parser instance is needed.
    HttpParser                       _parser;
};

#endif // EVENT_LOOP_HPP
