# Webserv — Bugs Report
# Code Review Against Subject PDF (webserv_subject.pdf)

---

## HOW TO USE THIS REPORT

Each bug in **Part 3** has a corresponding directory under `bug_tests/`:

```
bug_tests/
├── bug1_uaf_startcgi/        Use-after-free in _startCgi
├── bug2_eventref_leak/       Memory leak in _registerEventFd
├── bug3_epollout_dropped/    EPOLLOUT silently dropped on simultaneous EPOLLIN+EPOLLOUT
├── bug4_shutdown_leak/       Client EventRef/CgiJob leak on shutdown (Valgrind)
├── bug5_ipv4_cast/           IPv4-only cast without address-family check
├── bug6_const_cast/          const_cast mutation of a const-reference parameter
└── bug7_chunked_crlf/        Chunked body trailing CRLF not validated
```

Each directory contains:
- `README.md`  — what the bug is, where it lives, how to reproduce it
- `trigger.py` (or `.sh`) — runnable test that demonstrates the bug
- `run_test.sh` — one-liner wrapper that starts the server and fires the test

---

## PART 1 — SUBJECT VIOLATIONS (Grade-0 Risk)

### VIOLATION 1 — errno checked after read/write (×4 sites)

**Subject says:**
> "Checking the value of errno to adjust the server behaviour is strictly forbidden
>  after performing a read or write operation."

| File | Function | Syscall | Line (approx) |
|------|----------|---------|---------------|
| src/EventLoop.cpp | `_handleRead` | `recv()` | ~712 |
| src/EventLoop.cpp | `_handleWrite` | `send()` | ~794 |
| src/EventLoop.cpp | `_handleCgiStdinEvent` | `write()` | ~379 |
| src/EventLoop.cpp | `_handleCgiEvent` | `read()` | ~422 |

Pattern in all four locations:
```cpp
ssize_t n = conn->recv(...);
if (n < 0)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)  // ← FORBIDDEN BY SUBJECT
        break;
    ...
}
```

**Fix:** Remove inner drain loops. In edge-triggered epoll, each event triggers exactly
one syscall; if it returns -1, close the connection — do not branch on errno.

---

### VIOLATION 2 — `_stubCgi()` returns 501 — stub left in production code

`ResponseHandler::handle()` step 7 (line 270) calls `_stubCgi()` which outputs:
```
[ResponseHandler] CGI requested — Phase 4 not yet integrated
```
...to stderr and returns 501. Evaluators running curl or strace will see this.
In normal operation the EventLoop intercepts CGI before `handle()` is called,
but the stub is still compiled-in and reachable.

**Fix:** Delete `_stubCgi()` and its call site inside `handle()`.

---

### VIOLATION 3 — `fahd.conf` has hardcoded absolute path

```nginx
root /home/fahd/fork/webserer_core/www/html;
```

The config parser calls `stat()` on this path at startup and fatals if it is missing.
On every evaluator machine the path doesn't exist → server refuses to start.

**Fix:** Change to `./www/html` (already fixed in `replit.conf`).

---

### VIOLATION 4 — 405 responses missing mandatory `Allow` header

RFC 7231 §6.5.5:
> "An origin server MUST generate an Allow header field in a 405 response."

`_sendErrorInternal(405, ...)` emits a plain 405 with no `Allow` header.

**Fix:** Add `Allow: GET, POST, DELETE\r\n` (using the location's actual allowed methods)
to every 405 response.

---

## PART 2 — CRASHES / UNDEFINED BEHAVIOUR

### BUG 1 — Use-after-free in `_startCgi` when `_rearmClient` closes the connection

**File:** `src/EventLoop.cpp` — `_startCgi()`
**Severity:** Crash (SIGSEGV)
**Test directory:** `bug_tests/bug1_uaf_startcgi/`

```cpp
conn->setCgiRunning();
_rearmClient(conn->fd());           // (A) — can delete conn via closeConnection()

// ... CGI fork setup ...

if (!ok) {
    _responder.sendError(500, *conn->config(), ...); // UAF if (A) deleted conn
    conn->setWriting();                              // UAF
    _rearmClient(conn->fd());                        // UAF
}
```

`_rearmClient` → `_modifyEventFd` → on `epoll_ctl(MOD)` failure →
`_manager->closeConnection(fd)` → `delete conn`.
The `!ok` error path then dereferences the freed pointer.

**Fix:**
```cpp
int client_fd = conn->fd();
conn->setCgiRunning();
_rearmClient(client_fd);
conn = _manager->get(client_fd);  // re-fetch; may be NULL now
if (!conn) return;                // connection already closed
```

---

### BUG 2 — Memory leak in `_registerEventFd` when `epoll_ctl ADD` fails

**File:** `src/EventLoop.cpp` — `_registerEventFd()`
**Severity:** Memory leak (flagged by Valgrind)
**Test directory:** `bug_tests/bug2_eventref_leak/`

```cpp
ref = new EventRef(kind, fd);
_event_refs[fd] = ref;          // stored in map BEFORE epoll_ctl
if (::epoll_ctl(..., EPOLL_CTL_ADD, ...) < 0)
    throw std::runtime_error(...);  // ref stays in map — never freed
```

When the throw propagates, the caller closes the fd but never calls
`_unregisterEventFd`, so the `EventRef*` is orphaned.

**Fix:** Insert into `_event_refs` only after `epoll_ctl` succeeds:
```cpp
if (::epoll_ctl(...) < 0) throw std::runtime_error(...);
_event_refs[fd] = ref;  // reached only on success
```

---

### BUG 3 — EPOLLOUT silently dropped when EPOLLIN and EPOLLOUT arrive together

**File:** `src/EventLoop.cpp` — `_handleClientEvent()`
**Severity:** Permanent write stall (connection hangs)
**Test directory:** `bug_tests/bug3_epollout_dropped/`

```cpp
if (events & EPOLLIN)
{
    _handleRead(conn);
    return;      // ← early return — EPOLLOUT never checked
}
if (events & EPOLLOUT)
{
    _handleWrite(conn);
    return;
}
```

In edge-triggered epoll, the kernel may deliver `EPOLLIN|EPOLLOUT` in a single
event. The early `return` after `_handleRead` discards the EPOLLOUT bit.
No new EPOLLOUT will arrive until data is written → connection stalls permanently.

**Fix:**
```cpp
if (events & EPOLLIN)  _handleRead(conn);
conn = _manager->get(fd);       // re-fetch — _handleRead may have closed it
if (conn && (events & EPOLLOUT)) _handleWrite(conn);
```

---

### BUG 4 — Client EventRef and CgiJob objects leaked on shutdown

**File:** `src/EventLoop.cpp` — `stop()`
**Severity:** Memory leak (flagged by Valgrind with "definitely lost")
**Test directory:** `bug_tests/bug4_shutdown_leak/`

`stop()` iterates only `_server_fds` when freeing `_event_refs`:
```cpp
for (size_t i = 0; i < _server_fds.size(); ++i) {
    ...
    delete _event_refs[fd];   // only server EventRef freed
}
```

All client `EventRef*` entries in `_event_refs`, plus all `CgiJob*` entries in
`_cgi_jobs` and `_cgi_stdin_jobs`, are never deleted.

**Fix:**
```cpp
// in stop(), after the server_fd loop:
for (std::map<int,EventRef*>::iterator it = _event_refs.begin();
     it != _event_refs.end(); ++it)
    delete it->second;
_event_refs.clear();

for (std::map<int,CgiJob*>::iterator it = _cgi_jobs.begin();
     it != _cgi_jobs.end(); ++it)
    delete it->second;
_cgi_jobs.clear();
```

---

### BUG 5 — `addrToString` does an unconditional IPv4 cast without checking address family

**File:** `src/ConnectionManager.cpp` — `addrToString()`
**Severity:** UB / garbage REMOTE_ADDR in CGI environment
**Test directory:** `bug_tests/bug5_ipv4_cast/`

```cpp
const struct sockaddr_in* sin =
    reinterpret_cast<const struct sockaddr_in*>(&addr);  // no ss_family check
char buf[INET_ADDRSTRLEN];
::inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
```

If the OS dual-stacks the listening socket and delivers an IPv6-mapped IPv4
address (`::ffff:1.2.3.4`), the cast reads garbage bytes as `sin_addr`,
producing a wrong `REMOTE_ADDR` CGI variable and a potentially wrong
`Connection:` log entry.

**Fix:**
```cpp
if (addr.ss_family == AF_INET6) {
    const struct sockaddr_in6* sin6 =
        reinterpret_cast<const struct sockaddr_in6*>(&addr);
    char buf[INET6_ADDRSTRLEN];
    ::inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf));
    return std::string(buf);
}
// else fall through to AF_INET path
```

---

### BUG 6 — `const_cast` mutation of a const-reference parameter

**File:** `src/ResponseHandler.cpp` — `handle()`
**Severity:** Undefined behaviour (if original object is ever const-qualified)
**Test directory:** `bug_tests/bug6_const_cast/`

```cpp
void ResponseHandler::handle(const HttpRequest& req, ...) {
    ...
    const_cast<HttpRequest&>(req).path = safe_path;  // mutates caller's object
```

The function signature promises not to modify `req`, but it secretly writes back
through the const reference. Any compiler that observes the `const` contract and
places the original in read-only memory (valid per the standard) will produce a
segfault here. It also silently changes the state of `Connection::_request`
(which owns the object) for the remainder of the request.

**Fix:** Use a local variable:
```cpp
std::string resolved_path = safe_path;
// use resolved_path everywhere instead of req.path
```

---

### BUG 7 — Chunked body trailing CRLF consumed without validation

**File:** `src/HttpParser.cpp` — `_parseChunked()`
**Severity:** Silent protocol violation (malformed request accepted as valid)
**Test directory:** `bug_tests/bug7_chunked_crlf/`

```cpp
if (req._chunk_trailing) {
    if (buf.size() < 2) return;
    buf.consume(2);               // ← blindly discards 2 bytes, no CRLF check
    req._chunk_trailing = false;
```

The parser checks that 2 bytes are present, but never checks that they are `\r\n`.
A client sending `\r\x00`, `\n\n`, or `XX` after chunk data will have those bytes
silently discarded and the request accepted as valid. This can be used to smuggle
data past a downstream proxy that validates strictly.

**Fix:**
```cpp
if (buf.size() < 2) return;
if (buf.data()[0] != '\r' || buf.data()[1] != '\n') {
    req.parse_state = PSTATE_ERROR;
    req.error_code  = 400;
    return;
}
buf.consume(2);
```

---

## PART 3 — DEAD CODE

| Location | Item | Status |
|----------|------|--------|
| `src/EventLoop.cpp` + `EventLoop.hpp` | `_isServerFd()` | Implemented, never called |
| `src/ConnectionManager.cpp` + `.hpp` | `rearmEpoll()` | Implemented, never called |
| `src/Connection.cpp` + `.hpp` | `setClosing()` | Implemented, never called |
| `src/ConnectionState.cpp` | `connStateStr()` | Implemented, never called |
| `src/Logger.cpp` | `perf()`, `state()`, `setLevel()`, `getLevel()` | Implemented, never called |
| `cgi/CgiHandler.hpp` | `CGI_IDLE`, `CGI_WRITING_STDIN`, `CGI_READING`, `CGI_DONE` | Enum values never assigned |
| `src/ResponseHandler.cpp` | `_stubCgi()` + CGI detection in `handle()` step 7 | Unreachable at runtime |
| `src/main.cpp` ~L48–97 | Old `make_listener()` block | Commented-out dead code |
| `src/ConnectionManager.cpp` ~L84–93 | try/catch around `new Connection()` | Commented-out dead code |

---

## PART 4 — REDUNDANCIES

| Location | Issue |
|----------|-------|
| `EventLoop::_handleRead` + `ResponseHandler::handle` | `cfg.matchLocation()` called twice for every non-CGI request |
| `EventLoop::_modifyEventFd` | `kind` parameter accepted but immediately discarded with `(void)kind` |
| `Logger::Stopwatch::elapsed_ms()` | Uses `time_t` (1 s resolution), multiplies by 1000, labels result "ms" — always a multiple of 1000 |
| `ResponseHandler::_handlePost` | `static int counter` wraps at `INT_MAX` |
| `ConnectionManager::closeConnection` + `EventLoop::_closeClient` | Both call `epoll_ctl(EPOLL_CTL_DEL)` on the same fd — second call always gets ENOENT |
| `HttpParser::_parseHeaders` — `validate_host` | `port_out` extracted but caller never uses it |
