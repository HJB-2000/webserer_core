# Non-blocking CGI landing-zone implementation plan

## Goal

Build only the core-side landing zone for CGI:

- core detects a CGI request
- core creates an fd that can wake `epoll`
- core passes that fd to teammate's CGI function
- teammate owns fork/exec/pipes and writes the CGI result to the fd
- core receives readiness on that fd, reads the CGI result, then sends the HTTP response to the original client

The parent event loop must never wait for CGI synchronously.

## Teammate boundary / contract

Agree on a function shaped like this conceptually:

```cpp
bool startCgi(
    const HttpRequest& req,
    const ServerConfig& cfg,
    const Location& loc,
    const std::string& script_path,
    int result_write_fd
);
```

Core responsibility:

1. Create the result channel.
2. Pass the write side fd to `startCgi`.
3. Close the write side in the parent after `startCgi` returns.
4. Register the read side in `epoll`.
5. Later read the CGI result and convert it into a normal client response.

Teammate responsibility:

1. Fork child / setup CGI stdin/stdout/stderr.
2. Execute interpreter/script.
3. Collect CGI output.
4. Write final CGI bytes to `result_write_fd`.
5. Close `result_write_fd` when done.

Important contract: teammate's `startCgi` must return quickly in the parent. If it waits for the child, the core design is defeated.

## Use pipe or socketpair?

Recommended for this contract: `pipe()`.

```cpp
int cgi_pipe[2];
pipe(cgi_pipe);
// cgi_pipe[0] = parent read end, register in epoll
// cgi_pipe[1] = teammate write end, passed to startCgi
```

Set `cgi_pipe[0]` non-blocking before adding it to epoll.

If teammate also needs bidirectional streaming later, switch to `socketpair()`, but for "CGI writes result back to core", a pipe is enough.

## New core object: CgiJob

Create a small object owned by the event loop or a `CgiManager`.

```cpp
struct CgiJob
{
    int         client_fd;
    int         result_fd;
    Buffer      result_buffer;
    time_t      start_time;
    bool        eof;
};
```

Key:

- `client_fd` tells us which client receives the final response.
- `result_fd` is the fd registered in `epoll`.
- `result_buffer` stores raw CGI output until EOF.
- `start_time` lets the event loop enforce CGI timeout.

Store jobs in:

```cpp
std::map<int, CgiJob*> _cgi_jobs; // key = result_fd
```

Do not pack `client_fd` and `cgi_fd` into one 8-byte integer. It is fragile and makes cleanup/debugging harder.

## New epoll event typing

Current code uses:

- server fd: `ev.data.fd`
- client fd: `ev.data.ptr = Connection*`

CGI adds a third kind, so dispatch needs explicit event type.

Create:

```cpp
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
};
```

Each registered fd gets a stable heap-owned `EventRef`.

Examples:

```cpp
server_ref.kind = EV_SERVER;
server_ref.fd = server_fd;

client_ref.kind = EV_CLIENT;
client_ref.fd = client_fd;

cgi_ref.kind = EV_CGI;
cgi_ref.fd = result_fd;
```

Then `epoll_event.data.ptr = EventRef*`.

Dispatch becomes:

```cpp
EventRef* ref = static_cast<EventRef*>(ev.data.ptr);
if (ref->kind == EV_SERVER) _handleAccept(ref->fd);
if (ref->kind == EV_CLIENT) _handleClientEvent(ref->fd, ev.events);
if (ref->kind == EV_CGI)    _handleCgiEvent(ref->fd, ev.events);
```

This is safer than relying on `ev.data.fd` sometimes and `ev.data.ptr` other times.

## New connection state

Add:

```cpp
CSTATE_CGI_RUNNING
```

Meaning:

- request is complete
- core already started CGI
- no response is ready yet
- client fd should not be watched for `EPOLLOUT`
- CGI result fd is watched for `EPOLLIN`

`Connection::buildEpollEvent()` should not add `EPOLLIN` or `EPOLLOUT` for `CSTATE_CGI_RUNNING`.

## Detect CGI without immediately responding

Current `ResponseHandler::handle()` calls private `_stubCgi()` and writes `501`.

For a clean landing zone, split the decision from response building:

Option A, minimal change:

- Add return status to `ResponseHandler::handle()`.

```cpp
enum ResponseResult
{
    RESPONSE_READY,
    RESPONSE_CGI_NEEDED
};
```

When a CGI match happens:

- do not write `501`
- store enough CGI info in an output object
- return `RESPONSE_CGI_NEEDED`

Option B, cleaner:

- Add a resolver method:

```cpp
bool ResponseHandler::isCgiRequest(
    const HttpRequest& req,
    const ServerConfig& cfg,
    const Location** loc_out,
    std::string* script_path_out
) const;
```

Then `EventLoop::_handleRead()` does:

1. parse request
2. if complete, ask responder if this is CGI
3. if CGI, call `_startCgi(conn, loc, script_path)`
4. else call normal `handle()`

Recommended: Option B. It keeps event-loop async behavior outside `ResponseHandler`, and `ResponseHandler` remains responsible for path/location decisions.

## Start CGI flow

Add a method:

```cpp
void EventLoop::_startCgi(Connection* conn,
                          const Location& loc,
                          const std::string& script_path);
```

Steps:

1. `pipe(cgi_pipe)`
2. set `cgi_pipe[0]` non-blocking
3. create `CgiJob(client_fd=conn->fd(), result_fd=cgi_pipe[0])`
4. insert job into `_cgi_jobs`
5. add `cgi_pipe[0]` to epoll as `EV_CGI`
6. call teammate function with `cgi_pipe[1]`
7. close `cgi_pipe[1]` in parent
8. set connection state to `CSTATE_CGI_RUNNING`
9. rearm client fd so it has no read/write interest while CGI runs

If any step fails:

- close both pipe fds
- remove job if inserted
- write `500 Internal Server Error` to client buffer
- switch client to `CSTATE_WRITING`

## CGI fd event flow

Add:

```cpp
void EventLoop::_handleCgiEvent(int result_fd, uint32_t events);
```

On `EPOLLIN`:

- read in a loop until `EAGAIN`
- append bytes to `job->result_buffer`
- if `read()` returns `0`, CGI result is complete

On EOF:

1. find `Connection* conn = _manager->get(job->client_fd)`
2. if client is gone, destroy job only
3. parse CGI output:
   - CGI output may contain headers then blank line
   - example: `Status: 200 OK\r\nContent-Type: text/html\r\n\r\n<body>`
4. write final HTTP response into `conn->writeBuffer()`
5. set `conn->setWriting()`
6. rearm client fd for `EPOLLOUT`
7. remove CGI fd from epoll
8. close CGI fd
9. delete job

On `EPOLLERR` / `EPOLLHUP`:

- treat as EOF if buffer has data
- otherwise generate `502 Bad Gateway` or `500`

## CGI output parsing

Core should convert CGI-style output into HTTP response.

Minimum parser:

1. Find header/body separator:
   - prefer `\r\n\r\n`
   - optionally accept `\n\n`
2. Parse header lines.
3. If a `Status:` header exists, use that status code.
4. Copy CGI headers except `Status`.
5. Add missing core headers:
   - `Date`
   - `Server`
   - `Content-Length`
   - `Connection`
6. Append body.

If CGI output has no header separator, return `502 Bad Gateway`.

## Cleanup rules

When client closes while CGI is running:

- close client normally
- unregister CGI fd
- close CGI result fd
- delete `CgiJob`
- optional: tell teammate whether core should kill child or teammate owns that

When CGI times out:

- unregister and close CGI fd
- delete job
- write `504 Gateway Timeout` to client if still connected
- switch client to `WRITING`

When event loop shuts down:

- close all CGI fds
- delete all `CgiJob`s
- close all `EventRef`s

## Timeout sweep

Add a CGI timeout pass after each `epoll_wait`, similar to connection timeout:

```cpp
_closeTimedOutCgiJobs();
```

Default timeout can be 10 seconds until config exposes a CGI timeout.

## Required code touch points

1. `Headers/ConnectionState.hpp`
   - add `CSTATE_CGI_RUNNING`

2. `src/ConnectionState.cpp`
   - add string label

3. `Headers/Connection.hpp` / `src/Connection.cpp`
   - add `setCgiRunning()`
   - update `buildEpollEvent()`

4. `Headers/EventLoop.hpp`
   - add `_cgi_jobs`
   - add `_handleCgiEvent`
   - add `_startCgi`
   - add `_closeCgiJob`
   - add event typing support

5. `src/EventLoop.cpp`
   - use typed `EventRef`
   - on CGI request, start CGI instead of writing response
   - handle CGI fd readiness
   - timeout and cleanup

6. `Headers/ResponseHandler.hpp` / `src/ResponseHandler.cpp`
   - expose a CGI detection/resolution method
   - expose a method to convert CGI output into HTTP response, or implement that in a separate `CgiResponseParser`

7. New header/source, optional:
   - `Headers/CgiJob.hpp`
   - `Headers/EventRef.hpp`
   - `src/CgiResponseParser.cpp`

## First safe milestone

Before teammate's real CGI exists, fake the teammate function:

```cpp
bool startCgi(..., int result_write_fd)
{
    std::string out =
        "Status: 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "fake cgi ok\n";
    write(result_write_fd, out.c_str(), out.size());
    close(result_write_fd);
    return true;
}
```

This tests your core landing zone:

- CGI fd wakes epoll
- core maps CGI fd back to client fd
- core writes final HTTP response
- client receives response
- no parent blocking

After that works, replace fake function with teammate's real function.

## Main mental model

Client fd is for HTTP socket I/O.

CGI result fd is a temporary event source that says:

> "The response body for this client is ready or progressing."

The bridge between them is `CgiJob`.
