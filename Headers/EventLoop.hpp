// ============================================================
//  EventLoop.hpp — declarations only
//
//  The core epoll dispatch loop.  C++98 compliant.
//
//  Integration seams status:
//    Phase 2 ✓  _parser.feed()       replaces _stubParse()
//    Phase 3 ✓  _responder.handle()  replaces _stubBuildResponse()
//               _responder.sendError replaces _stubSend400/_stub413
//    Phase 4    _stubCgi in ResponseHandler → CgiHandler::execute()
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
#include <map>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <stdexcept>
#include <sys/wait.h>
#include <signal.h>

#include "ConnectionManager.hpp"
#include "Connection.hpp"
#include "HttpParser.hpp"
#include "ResponseHandler.hpp"
#include "CgiJob.hpp"
#include "CgiRequestInfo.hpp"
#include "EventRef.hpp"


class EventLoop
{
public:

    EventLoop();
    ~EventLoop();
    static const int MAX_EVENTS       = 4096;
    static const int EPOLL_TIMEOUT_MS = 100;
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

    // ── members ────────────────────────────────────────────
    int                              _epoll_fd;
    ConnectionManager*               _manager;
    volatile sig_atomic_t            _running;   // atomic for signal safety
    volatile sig_atomic_t            _stopped;   // set true when stop() called
    std::vector<int>                 _server_fds;
    std::vector<const ServerConfig*> _server_configs;
    std::map<int, CgiJob*>   _cgi_jobs;
    std::map<int, EventRef*> _event_refs;
    std::vector<EventRef*>   _stale_refs;

    // Phase 2: one stateless parser serves all connections.
    // All parse progress lives in HttpRequest, so no per-connection
    // parser instance is needed.
    HttpParser                       _parser;

    // Phase 3: builds HTTP responses from completed requests.
    ResponseHandler                  _responder;

    //CGI + client event dispatch
    void _handleClientEvent(int client_fd, uint32_t events);
    void _handleCgiEvent(int result_fd, uint32_t events);

    void _startCgi(Connection* conn, const CgiRequestInfo& info);
    void _addCgiFd(int result_fd, int client_fd);
    void _finishCgiJob(int result_fd);
    void _failCgiJob(int result_fd, int status_code);
    void _closeCgiJob(int result_fd);
    void _closeCgiJobsForClient(int client_fd);
    void _closeTimedOutCgiJobs();

    // CGI stdin (non-blocking body writer) — driven by EPOLLOUT
    void _handleCgiStdinEvent(int stdin_fd, uint32_t events);
    void _closeCgiStdin(CgiJob* job);

    // stdin_fd -> CgiJob*, so EPOLLOUT on the child's stdin pipe can find the job.
    std::map<int, CgiJob*> _cgi_stdin_jobs;

    // Deferred-reap list: PIDs that were SIGKILL'd during _closeCgiJob but
    // did not yet exit (e.g. child in D-state). Drained non-blockingly with
    // waitpid(WNOHANG) on every event-loop iteration, so the loop never
    // blocks waiting for a stuck child. Entries older than 60 s are dropped
    // to prevent unbounded growth from children stuck in D-state.
    static const int REAP_STALE_SECONDS = 60;
    std::vector< std::pair<pid_t, time_t> > _pending_reap;
    void _reapPending();

    // unified client close (CGI cleanup + EventRef cleanup + conn close)
    void _closeClient(int fd);
    void _rearmClient(int fd);
    void _closeTimedOutClients();

    static bool _setCloexec(int fd, const char* label);

    void _registerEventFd(int fd, EventKind kind, uint32_t events);
    void _modifyEventFd(int fd, EventKind kind, uint32_t events);
    void _unregisterEventFd(int fd);
};

int make_listener(const char* host, int port);

#endif // EVENT_LOOP_HPP
