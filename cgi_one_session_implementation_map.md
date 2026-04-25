# CGI core landing zone — one-session implementation map

This file lists the exact functions/classes to add, where to call them, and the recommended implementation order.

Scope: core side only.

Assumption: teammate provides a CGI starter function that takes an fd. Teammate owns fork/exec/pipes and writes the CGI result to the fd you pass.

---

## 0. Big picture call flow

Current normal response path:

```cpp
EventLoop::_handleRead()
    -> parser.feed()
    -> if PSTATE_COMPLETE
        -> _responder.handle(...)
        -> conn->setWriting()
        -> _manager->rearmEpoll(client_fd)
```

New CGI path:

```cpp
EventLoop::_handleRead()
    -> parser.feed()
    -> if PSTATE_COMPLETE
        -> if _responder.resolveCgiRequest(...)
            -> _startCgi(conn, cgi_info)
            -> return
        -> else normal _responder.handle(...)
```

Later:

```cpp
epoll_wait wakes on cgi_result_fd
    -> EventLoop::_handleCgiEvent(cgi_result_fd, events)
    -> read result
    -> when EOF, convert CGI output to HTTP response
    -> put response in original client's writeBuffer
    -> conn->setWriting()
    -> _manager->rearmEpoll(client_fd)
```

---

## 1. Add `CSTATE_CGI_RUNNING`

### File: `Headers/ConnectionState.hpp`

Add one enum value:

```cpp
enum ConnectionState
{
    CSTATE_READING = 0,
    CSTATE_PROCESSING,
    CSTATE_CGI_RUNNING,
    CSTATE_WRITING,
    CSTATE_CLOSING
};
```

Meaning: request is complete, CGI was started, no response is ready yet.

### File: `src/ConnectionState.cpp`

Add label:

```cpp
case CSTATE_CGI_RUNNING: return "CGI_RUNNING";
```

---

## 2. Add `Connection::setCgiRunning()`

### File: `Headers/Connection.hpp`

Add public method near state transitions:

```cpp
void setCgiRunning();
```

### File: `src/Connection.cpp`

Implement:

```cpp
void Connection::setCgiRunning()
{
    _state = CSTATE_CGI_RUNNING;
}
```

### File: `src/Connection.cpp`

In `Connection::buildEpollEvent()`, do not add `EPOLLIN` or `EPOLLOUT` for this state.

No special code needed if the `default:` branch already means "no I/O interest":

```cpp
default:
    break;
```

But update the comment so `CSTATE_CGI_RUNNING` is clearly included.

---

## 3. Add CGI request info object

### File: new `Headers/CgiRequestInfo.hpp`

Add:

```cpp
#ifndef CGI_REQUEST_INFO_HPP
#define CGI_REQUEST_INFO_HPP

#include <string>
#include "serverConfig.hpp"

struct CgiRequestInfo
{
    const Location* location;
    std::string     script_path;
};

#endif
```

Purpose: `ResponseHandler` can tell `EventLoop` "this request is CGI" without actually starting CGI.

---

## 4. Add CGI detection function to `ResponseHandler`

### File: `Headers/ResponseHandler.hpp`

Include:

```cpp
#include "CgiRequestInfo.hpp"
```

Add public method:

```cpp
bool resolveCgiRequest(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    CgiRequestInfo&     out
) const;
```

### File: `src/ResponseHandler.cpp`

Implement using the same logic currently inside `handle()`:

```cpp
bool ResponseHandler::resolveCgiRequest(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    CgiRequestInfo&     out) const
{
    const Location* loc = cfg.matchLocation(req.path);
    if (!loc || loc->getCGI_extension().empty())
        return false;

    const std::string ext = loc->getCGI_extension();
    if (req.path.size() < ext.size())
        return false;

    if (req.path.compare(req.path.size() - ext.size(), ext.size(), ext) != 0)
        return false;

    out.location = loc;
    out.script_path = _resolveFsPath(req, loc, cfg);
    return true;
}
```

### Important

For the first landing-zone version, leave the existing `_stubCgi()` in `handle()` as fallback.

The event loop will detect CGI before calling `handle()`, so `_stubCgi()` should not be reached for the new path.

---

## 5. Add `CgiJob`

### File: new `Headers/CgiJob.hpp`

```cpp
#ifndef CGI_JOB_HPP
#define CGI_JOB_HPP

#include <ctime>
#include "buffer.hpp"

struct CgiJob
{
    int    client_fd;
    int    result_fd;
    Buffer result_buffer;
    time_t start_time;

    CgiJob(int cfd, int rfd, size_t max_size)
        : client_fd(cfd)
        , result_fd(rfd)
        , result_buffer(max_size)
        , start_time(std::time(NULL))
    {}
};

#endif
```

`result_fd` is the parent read end registered in epoll.

---

## 6. Add event type object for epoll

Current code mixes:

- server fds: `ev.data.fd`
- client fds: `ev.data.ptr = Connection*`

CGI adds a third type. Make this explicit.

### File: new `Headers/EventRef.hpp`

```cpp
#ifndef EVENT_REF_HPP
#define EVENT_REF_HPP

enum EventKind
{
    EV_SERVER,
    EV_CLIENT,
    EV_CGI
};

struct EventRef
{
    EventKind kind;
    int       fd;

    EventRef(EventKind k, int f) : kind(k), fd(f) {}
};

#endif
```

### Required ownership map

In `EventLoop`, store:

```cpp
std::map<int, EventRef*> _event_refs;
```

Key is fd. Value is stable heap pointer placed in `epoll_event.data.ptr`.

---

## 7. Update `EventLoop.hpp`

### File: `Headers/EventLoop.hpp`

Add includes:

```cpp
#include <map>
#include "CgiJob.hpp"
#include "CgiRequestInfo.hpp"
#include "EventRef.hpp"
```

Add private functions:

```cpp
void _handleClientEvent(int client_fd, uint32_t events);
void _handleCgiEvent(int result_fd, uint32_t events);

void _startCgi(Connection* conn, const CgiRequestInfo& info);
void _addCgiFd(int result_fd, int client_fd);
void _closeCgiJob(int result_fd);
void _closeCgiJobsForClient(int client_fd);
void _closeTimedOutCgiJobs();

void _registerEventFd(int fd, EventKind kind, uint32_t events);
void _modifyEventFd(int fd, EventKind kind, uint32_t events);
void _unregisterEventFd(int fd);
```

Add members:

```cpp
std::map<int, CgiJob*>   _cgi_jobs;
std::map<int, EventRef*> _event_refs;
```

---

## 8. Replace epoll registration with typed `EventRef`

### File: `src/EventLoop.cpp`

Add helper:

```cpp
void EventLoop::_registerEventFd(int fd, EventKind kind, uint32_t events)
{
    EventRef* ref = new EventRef(kind, fd);
    _event_refs[fd] = ref;

    epoll_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.ptr = ref;

    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error(std::string("epoll_ctl ADD failed: ") + std::strerror(errno));
}
```

Add unregister helper:

```cpp
void EventLoop::_unregisterEventFd(int fd)
{
    ::epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    std::map<int, EventRef*>::iterator it = _event_refs.find(fd);
    if (it != _event_refs.end())
    {
        delete it->second;
        _event_refs.erase(it);
    }
}
```

You can add `_modifyEventFd()` similarly using `EPOLL_CTL_MOD`.

### Update `addServerSocket()`

Replace manual `epoll_ctl` with:

```cpp
_registerEventFd(server_fd, EV_SERVER, EPOLLIN | EPOLLET);
```

Keep:

```cpp
_server_fds.push_back(server_fd);
_server_configs.push_back(config);
```

---

## 9. Update client fd registration

Current client fd registration happens in `ConnectionManager::addConnection()` using `conn->buildEpollEvent()` and `ev.data.ptr = Connection*`.

For the clean typed-event version, one of these must happen:

### Recommended clean version

Move epoll ADD/MOD for client fds out of `ConnectionManager` and into `EventLoop`, because only `EventLoop` knows `EventRef`.

That means:

- `ConnectionManager::addConnection()` accepts and creates `Connection`
- `EventLoop::_handleAccept()` registers the returned client fd with `_registerEventFd(client_fd, EV_CLIENT, EPOLLIN | EPOLLET | EPOLLRDHUP)`
- `ConnectionManager::rearmEpoll()` either moves to EventLoop or gets replaced by `EventLoop::_rearmClient(conn)`

### Smaller version

Keep current client registration for now and only use typed event refs for CGI.

But then `_dispatch()` must support mixed old + new style:

- server fd detected by `_isServerFd(ev.data.fd)`
- CGI fd detected by `_cgi_jobs.count(ev.data.fd)` only if you use `ev.data.fd`
- client uses `ev.data.ptr`

This is less clean and easier to break.

### For one clean implementation session

Use the clean version if you have time. If short on time, use the smaller version and refactor later.

---

## 10. Add `_addCgiFd()`

### File: `src/EventLoop.cpp`

```cpp
void EventLoop::_addCgiFd(int result_fd, int client_fd)
{
    _registerEventFd(result_fd, EV_CGI, EPOLLIN | EPOLLET | EPOLLHUP | EPOLLERR);
    std::cerr << "[EventLoop] CGI fd " << result_fd
              << " registered for client fd " << client_fd << "\n";
}
```

Call this only from `_startCgi()`.

Do not call `addServerSocket()` for CGI fds.

---

## 11. Add teammate function adapter

Create a temporary adapter so your code compiles before teammate integrates.

### File: new `Headers/CgiStarter.hpp`

```cpp
#ifndef CGI_STARTER_HPP
#define CGI_STARTER_HPP

#include <string>
#include "HttpRequest.hpp"
#include "serverConfig.hpp"

bool startCgi(
    const HttpRequest& req,
    const ServerConfig& cfg,
    const Location& loc,
    const std::string& script_path,
    int result_write_fd
);

#endif
```

### File: temporary `src/CgiStarter.cpp`

```cpp
#include "Headers/CgiStarter.hpp"
#include <unistd.h>
#include <string>

bool startCgi(
    const HttpRequest&,
    const ServerConfig&,
    const Location&,
    const std::string&,
    int result_write_fd)
{
    std::string out =
        "Status: 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "fake cgi ok\n";
    ::write(result_write_fd, out.c_str(), out.size());
    return true;
}
```

Add `src/CgiStarter.cpp` to Makefile for local testing. Later teammate replaces implementation.

---

## 12. Add `_startCgi()`

### File: `src/EventLoop.cpp`

```cpp
void EventLoop::_startCgi(Connection* conn, const CgiRequestInfo& info)
{
    int fds[2];
    if (::pipe(fds) < 0)
    {
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _manager->rearmEpoll(conn->fd());
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
        _manager->rearmEpoll(conn->fd());
        return;
    }

    CgiJob* job = new CgiJob(conn->fd(), result_read_fd, conn->writeBuffer().maxSize());
    _cgi_jobs[result_read_fd] = job;
    _addCgiFd(result_read_fd, conn->fd());

    conn->setCgiRunning();
    _manager->rearmEpoll(conn->fd());

    bool ok = startCgi(conn->request(), *conn->config(), *info.location,
                       info.script_path, result_write_fd);

    ::close(result_write_fd);

    if (!ok)
    {
        _closeCgiJob(result_read_fd);
        _responder.sendError(500, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _manager->rearmEpoll(conn->fd());
    }
}
```

Important: parent closes `result_write_fd` after calling teammate function.

---

## 13. Call `_startCgi()` from `_handleRead()`

### File: `src/EventLoop.cpp`

Current call site:

```cpp
if (conn->request().parse_state == PSTATE_COMPLETE)
{
    conn->setProcessing();
    _responder.handle(...);
    conn->setWriting();
    _manager->rearmEpoll(fd);
    return;
}
```

Replace conceptually with:

```cpp
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
    _manager->rearmEpoll(fd);
    return;
}
```

This is the main call site.

---

## 14. Add `_handleCgiEvent()`

### File: `src/EventLoop.cpp`

```cpp
void EventLoop::_handleCgiEvent(int result_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    CgiJob* job = it->second;

    if (events & (EPOLLERR | EPOLLHUP))
    {
        // still try to drain; HUP often means writer closed after writing
    }

    char buf[8192];
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
```

This needs two helper functions:

```cpp
void _finishCgiJob(int result_fd);
void _failCgiJob(int result_fd, int status_code);
```

Add them to `EventLoop.hpp`.

---

## 15. Add `_finishCgiJob()`

### File: `src/EventLoop.cpp`

Responsibility:

1. Find job.
2. Find original client.
3. Convert CGI output to HTTP response.
4. Put response into client write buffer.
5. Switch client to writing.
6. Rearm client fd.
7. Delete CGI job.

Pseudo:

```cpp
void EventLoop::_finishCgiJob(int result_fd)
{
    CgiJob* job = _cgi_jobs[result_fd];
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.handleCgiOutput(conn->request(),
                                   *conn->config(),
                                   job->result_buffer,
                                   conn->writeBuffer());
        conn->setWriting();
        _manager->rearmEpoll(conn->fd());
    }

    _closeCgiJob(result_fd);
}
```

You need `ResponseHandler::handleCgiOutput(...)`.

---

## 16. Add `ResponseHandler::handleCgiOutput()`

### File: `Headers/ResponseHandler.hpp`

Add public method:

```cpp
void handleCgiOutput(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    const Buffer&       cgi_output,
    Buffer&             wb
);
```

### File: `src/ResponseHandler.cpp`

Minimum implementation:

1. Convert `cgi_output.data()` + `cgi_output.size()` to string.
2. Find `\r\n\r\n`.
3. If missing, send `502`.
4. Split headers/body.
5. Read optional `Status:` header.
6. Forward `Content-Type` and other safe CGI headers.
7. Add `Content-Length`, `Date`, `Server`, `Connection`.

First simple version can assume CGI returns:

```http
Status: 200 OK
Content-Type: text/plain

body...
```

Then harden later.

---

## 17. Add `_failCgiJob()`

### File: `src/EventLoop.cpp`

```cpp
void EventLoop::_failCgiJob(int result_fd, int status_code)
{
    CgiJob* job = _cgi_jobs[result_fd];
    Connection* conn = _manager->get(job->client_fd);

    if (conn)
    {
        _responder.sendError(status_code, *conn->config(), conn->writeBuffer());
        conn->setWriting();
        _manager->rearmEpoll(conn->fd());
    }

    _closeCgiJob(result_fd);
}
```

Use:

- `502` for bad CGI output/read error
- `504` for timeout
- `500` for setup failure

---

## 18. Add `_closeCgiJob()`

### File: `src/EventLoop.cpp`

```cpp
void EventLoop::_closeCgiJob(int result_fd)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    _unregisterEventFd(result_fd);
    ::close(result_fd);
    delete it->second;
    _cgi_jobs.erase(it);
}
```

Call this from:

- `_finishCgiJob`
- `_failCgiJob`
- timeout cleanup
- client close cleanup
- `EventLoop` destructor

---

## 19. Cleanup when client closes

Problem: if client fd closes while CGI is running, CGI job must not keep a dangling client fd.

Add:

```cpp
void EventLoop::_closeCgiJobsForClient(int client_fd);
```

Implementation:

```cpp
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
```

Call before any `_manager->closeConnection(fd)` in `EventLoop`.

Examples:

```cpp
_closeCgiJobsForClient(fd);
_manager->closeConnection(fd);
```

Call sites:

- `_handleError`
- `_handleRead` when `recv() == 0`
- `_handleRead` on recv error
- `_handleWrite` on send error
- `_handleWrite` when final close
- timeout close path if controlled by EventLoop

If connection timeout is inside `ConnectionManager::closeTimedOut()`, you may need to move timeout closing into `EventLoop` or add a callback-style cleanup later.

---

## 20. Add CGI timeout sweep

### File: `Headers/EventLoop.hpp`

Already listed:

```cpp
void _closeTimedOutCgiJobs();
```

### File: `src/EventLoop.cpp`

Call inside `EventLoop::run()` after connection timeout sweep:

```cpp
_manager->closeTimedOut();
_closeTimedOutCgiJobs();
```

Implementation:

```cpp
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
```

---

## 21. Update `_dispatch()`

### Clean typed-event version

```cpp
void EventLoop::_dispatch(const epoll_event& ev)
{
    EventRef* ref = static_cast<EventRef*>(ev.data.ptr);
    if (!ref)
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

    if (ref->kind == EV_CLIENT)
    {
        _handleClientEvent(ref->fd, ev.events);
        return;
    }
}
```

Move current client logic from `_dispatch()` into:

```cpp
void EventLoop::_handleClientEvent(int client_fd, uint32_t events);
```

Inside it:

```cpp
Connection* conn = _manager->get(client_fd);
if (!conn) return;
// then current EPOLLERR/EPOLLRDHUP/EPOLLIN/EPOLLOUT logic
```

---

## 22. Implementation order for one session

Do it in this order:

1. Add `CSTATE_CGI_RUNNING`.
2. Add `setCgiRunning()`.
3. Add `CgiRequestInfo.hpp`.
4. Add `ResponseHandler::resolveCgiRequest()`.
5. Add `CgiJob.hpp`.
6. Add temporary `CgiStarter.hpp/.cpp`.
7. Add EventLoop CGI job map and `_startCgi()`.
8. In `_handleRead()`, branch to `_startCgi()` before `_responder.handle()`.
9. Add `_addCgiFd()`.
10. Add `_handleCgiEvent()`.
11. Add `_finishCgiJob()` and `_failCgiJob()`.
12. Add `ResponseHandler::handleCgiOutput()`.
13. Add `_closeCgiJob()` cleanup.
14. Add timeout cleanup.
15. Refactor `_dispatch()` to support CGI fd events.
16. Build with `make -j1`.
17. Test with fake `startCgi()` first.
18. Replace fake starter with teammate implementation.

---

## 23. Minimal first test

Request:

```bash
curl -i http://127.0.0.1:8080/cgi-bin/hello.py
```

Expected with fake starter:

```http
HTTP/1.1 200 OK
Content-Type: text/plain
Content-Length: ...

fake cgi ok
```

Expected server behavior:

- client fd enters `CGI_RUNNING`
- CGI result fd is added to epoll
- epoll wakes on CGI fd
- result is copied to client write buffer
- client fd switches to `WRITING`
- response is sent without blocking the parent loop

---

## 24. The most important call sites

### Call `_startCgi()` here

File: `src/EventLoop.cpp`

Inside `_handleRead()`, in the `PSTATE_COMPLETE` block, before `_responder.handle()`.

### Call `_handleCgiEvent()` here

File: `src/EventLoop.cpp`

Inside `_dispatch()`, when event kind is `EV_CGI`.

### Call `_addCgiFd()` here

File: `src/EventLoop.cpp`

Inside `_startCgi()`, after `pipe()` and `setNonBlocking(result_read_fd)`.

### Call teammate `startCgi()` here

File: `src/EventLoop.cpp`

Inside `_startCgi()`, after registering the read fd and setting the connection to `CGI_RUNNING`.

### Call `handleCgiOutput()` here

File: `src/EventLoop.cpp`

Inside `_finishCgiJob()`, after EOF on CGI result fd.

### Call `_closeCgiJob()` here

File: `src/EventLoop.cpp`

At the end of `_finishCgiJob()`, `_failCgiJob()`, and every cleanup path.

---

## 25. Short answer to "where does CGI fd get added?"

Not in `addServerSocket()`.

Add it inside:

```cpp
EventLoop::_startCgi()
```

And call `_startCgi()` from:

```cpp
EventLoop::_handleRead()
```

when:

```cpp
conn->request().parse_state == PSTATE_COMPLETE
&& _responder.resolveCgiRequest(...)
```
