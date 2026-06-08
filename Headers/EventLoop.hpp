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

    EventLoop(const EventLoop&);
    EventLoop& operator=(const EventLoop&);

    void _dispatch(const epoll_event& ev);
    void _handleAccept(int server_fd);
    void _handleError(Connection* conn);
    void _handleRead(Connection* conn);
    void _handleWrite(Connection* conn);

    bool                _isServerFd(int fd) const;
    const ServerConfig* _configForServer(int fd) const;

    int                              _epoll_fd;
    ConnectionManager*               _manager;
    bool                             _running;
    std::vector<int>                 _server_fds;
    std::vector<const ServerConfig*> _server_configs;
    std::map<int, CgiJob*>   _cgi_jobs;
    std::map<int, EventRef*> _event_refs;
    std::vector<EventRef*>   _stale_refs;

    HttpParser                       _parser;

    ResponseHandler                  _responder;

    void _handleClientEvent(int client_fd, uint32_t events);
    void _handleCgiEvent(int result_fd, uint32_t events);

    void _ApiStartCgi(Connection* conn, const CgiRequestInfo& info);
    void _addCgiFd(int result_fd, int client_fd);
    void _finishCgiJob(int result_fd);
    void _failCgiJob(int result_fd, int status_code);
    void _closeCgiJob(int result_fd);
    void _closeCgiJobsForClient(int client_fd);
    void _closeTimedOutCgiJobs();

    void _handleCgiStdinEvent(int stdin_fd, uint32_t events);
    void _closeCgiStdin(CgiJob* job);

    std::map<int, CgiJob*> _cgi_stdin_jobs;

    static const int REAP_STALE_SECONDS = 60;
    std::vector< std::pair<pid_t, time_t> > _pending_reap;
    void _reapPending();

    void _closeClient(int fd);
    void _rearmClient(int fd);
    void _closeTimedOutClients();

    static bool _setCloexec(int fd, const char* label);

    void _registerEventFd(int fd, EventKind kind, uint32_t events);
    void _modifyEventFd(int fd, EventKind kind, uint32_t events);
    void _unregisterEventFd(int fd);
};

int make_listener(const char* host, int port);

#endif
