# Strict Review Report — FalcoBen/webserer_core

## 1. Violation Fixes Status

All 10 original violations from the previous review have been addressed:

| # | Violation | Fix | Status |
|---|-----------|-----|--------|
| 1 | `gettimeofday` in CgiHandler.cpp | Removed entirely (timing moved to CgiJob via `std::time`) | FIXED |
| 2 | `gmtime` in ResponseHandler.cpp | Changed to `std::gmtime` (C++ stdlib) | FIXED |
| 3 | `strftime` in ResponseHandler.cpp | Changed to `std::strftime` (C++ stdlib) | FIXED |
| 4 | `pipe2` in CgiHandler.cpp | Replaced with `pipe()` + `fcntl(F_SETFD, FD_CLOEXEC)` | FIXED |
| 5 | `pipe2` in EventLoop.cpp | Replaced with `pipe()` + `fcntl(F_SETFD, FD_CLOEXEC)` | FIXED |
| 6 | `getcwd` in CgiHandler.cpp | Removed — script paths are now absolute or resolved before chdir | FIXED |
| 7 | `unlink` in ResponseHandler.cpp | Replaced with `std::remove()` (C stdlib via `<cstdio>`) | FIXED |
| 8 | `fstat` in ResponseHandler.cpp | Replaced with `stat(fs_path.c_str(), &st)` | FIXED |
| 9 | `strtod` in parserConf.cpp | Custom `ft_strtod()` implementation | FIXED |
| 10 | `strtol` in httpConfig.cpp | Custom `safe_strtol()` implementation | FIXED |

### Previous Action Items Status

| Item | Status |
|------|--------|
| Update CgiJob.hpp with `_timeout_seconds` + 4-arg constructor | FIXED |
| CGI timeout uses per-job `_timeout_seconds` | FIXED |
| Client timeout skips CGI-running connections | FIXED |
| TOCTOU inode verification in child process | FIXED |

---

## 2. REMAINING FORBIDDEN FUNCTION VIOLATIONS

These are **still present** in the codebase and will cause a grade 0 if the evaluators catch them:

### CRITICAL — Logger.hpp (3 violations, each called multiple times)

| Line(s) | Function | Context |
|---------|----------|---------|
| 218, 224, 238 | `gettimeofday()` | `Logger::Stopwatch` — constructor, `elapsed_ms()`, `reset()` |
| 103, 259 | `::gmtime()` | `TeeStreambuf::_utcTimestamp()` and `Logger::_writeTagged()` |
| 105, 261 | `::strftime()` | `TeeStreambuf::_utcTimestamp()` and `Logger::_writeTagged()` |

**Severity: CRITICAL** — `gettimeofday` is **not** in the allowed list. `gmtime` and `strftime` are called via `::` (global scope, not `std::` namespace). While `std::gmtime`/`std::strftime` are arguably C++ stdlib, the `::` variants are POSIX and strictly speaking may be flagged by a strict evaluator.

**Fix**: Replace `gettimeofday` in Stopwatch with `std::time(NULL)` (gives second-level precision, which is fine for a log stopwatch). Use `std::gmtime` and `std::strftime` (with `std::` prefix) for the timestamp functions.

### MODERATE — Other borderline functions

| File | Line | Function | Risk |
|------|------|----------|------|
| `main.cpp` | 27 | `std::atoi(buf)` | C stdlib via `<cstdlib>` — generally safe but `safe_strtol` is available and safer |
| `ConnectionManager.cpp` | 47 | `std::atoi(buf)` | Same as above — used in `readSomaxconn()` |
| ✅`server_parser.cpp` | 25 | `atoi(s.c_str())` | Called without `std::` prefix in `is_valid_octet()` — bare POSIX call |
| `main.cpp` | 58 | `::inet_addr(host)` | **Not in allowed list** — this is a network function not listed in the subject |
| `ResponseHandler.cpp` | 640 | `::getpid()` | **Not in allowed list** — used for upload filename generation |

**`inet_addr` and `getpid` are NOT in the allowed function list.** These are real violations.

---

## 3. BUGS — Potential Crashes / Segfaults

### BUG 1: `_startCgi` — uncaught exception crashes the server (Severity: HIGH)

**File**: `EventLoop.cpp:275-288`

```cpp
CgiJob* job = NULL;
try {
    job = new CgiJob(...);
    _addCgiFd(result_read_fd, conn->fd());
    _cgi_jobs[result_read_fd] = job;
}
catch (const std::exception& ex)
{
    if (job != NULL) {
        delete job;
        job = NULL;
    }
    throw;  // <-- THIS THROW IS UNCAUGHT
}
```

The `throw` at line 287 propagates up to `_handleRead()`, which only catches `BodyLimitException`. Any `std::bad_alloc` or `std::runtime_error` from `new CgiJob` or `_addCgiFd` (which calls `_registerEventFd`, which throws on `epoll_ctl` failure) will **terminate the server**.

**Additionally**: When `_addCgiFd` throws (line 278), `result_write_fd` is **never closed** — FD leak.

**Fix**: Catch `std::exception` in `_handleRead` around the `_startCgi` call, or don't re-throw from the try-catch in `_startCgi` — instead send a 500 error and return gracefully. Also close `result_write_fd` in the catch block.

### BUG 2: `_registerEventFd` leaks EventRef on `epoll_ctl` failure (Severity: MEDIUM)

**File**: `EventLoop.cpp:109-130`

```cpp
void EventLoop::_registerEventFd(int fd, EventKind kind, uint32_t events)
{
    EventRef* ref = NULL;
    try {
        ref = new EventRef(kind, fd);
    }
    catch (const std::exception& e)
    {
        throw ;
    }
    _event_refs[fd] = ref;         // ref is stored

    epoll_event ev;
    ...
    if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error(...);  // ref is leaked!
}
```

If `epoll_ctl` fails, `ref` was inserted into `_event_refs` but never cleaned up properly — the exception propagates and the caller may not know to clean up the EventRef. The `_addCgiFd` wrapper closes the fd but doesn't remove the EventRef from the map.

**Fix**: On `epoll_ctl` failure, delete the EventRef and erase it from `_event_refs` before throwing.

### BUG 3: Signal handler calls non-async-signal-safe functions (Severity: MEDIUM)

**File**: `main.cpp:32-36`

```cpp
static void sig_handler(int)
{
    if (g_loop)
        g_loop->stop();
}
```

`stop()` calls `epoll_ctl`, `close`, `delete`, `_event_refs.count()`, `_event_refs.erase()` — none of which are async-signal-safe. If the signal arrives while the event loop is in the middle of a map operation, this causes **undefined behavior** (potential crash/corruption).

**Fix**: Use `volatile sig_atomic_t g_shutdown = 0;` in the signal handler, and check it in the event loop. Only call `stop()` from the main loop context.

### BUG 4: `gmtime()` returns a static pointer — not thread-safe (Severity: LOW)

**File**: `ResponseHandler.cpp:917`, `Logger.hpp:103,259`

`std::gmtime()` returns a pointer to a static `struct tm`. If any other code calls `gmtime`/`localtime` between the call and the use of the pointer, the data is silently overwritten. In a single-threaded server this is technically safe, but if you ever add threads (or if the signal handler triggers during `_httpDate()`), this is a **use-after-free**.

**Fix**: Use `gmtime_r()` — but it's not in the allowed list. For safety, copy the result immediately: `struct tm gmt = *std::gmtime(&now);`

### BUG 5: `_handleWrite` — `send()` returning 0 causes infinite loop (Severity: LOW)

**File**: `EventLoop.cpp:789-801`

```cpp
while (!conn->writeBuffer().empty())
{
    ssize_t n = conn->send();
    if (n < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        ...
    }
    // n == 0 is not handled — loops forever
}
```

`send()` can return 0 when the buffer is empty (checked at line 81 of Connection.cpp), but if `send()` returns 0 for any other reason (e.g., zero-length write buffer race), this loops forever.

The `Connection::send()` method does check for empty buffer and returns 0 early, so this is unlikely in practice, but defensively you should `break` on `n == 0`.

### BUG 6: `_setCloexec` failure on epoll_fd is silently ignored (Severity: LOW)

**File**: `EventLoop.cpp:79`

```cpp
_setCloexec(_epoll_fd, "epoll");
```

If this fails, the return value is ignored. The epoll fd will leak into child processes, which could cause subtle issues with CGI children inheriting the epoll fd.

**Fix**: Check return value and throw if it fails (same as `epoll_create` failure).

---

## 4. LOGIC / DESIGN ISSUES

### ISSUE 1: `result_write_fd` leaked on `_addCgiFd`/`new CgiJob` failure

**File**: `EventLoop.cpp:275-288`

When the try-catch block at line 276-288 catches an exception and re-throws, `result_write_fd` (line 264) is never closed. The `_addCgiFd` wrapper closes `result_read_fd` on failure, but `result_write_fd` stays open.

**Fix**: Add `::close(result_write_fd);` in the catch block before re-throwing.

### ISSUE 2: Double epoll_ctl DEL in `_closeClient` path

When `_closeClient(fd)` is called, it does:
1. `_closeCgiJobsForClient(fd)` — which calls `_closeCgiJob` → `_unregisterEventFd(result_fd)` (for CGI fds only)
2. `_unregisterEventFd(fd)` — removes client fd from epoll
3. `_manager->closeConnection(fd)` — which calls `epoll_ctl(DEL, fd)` again

So the client fd gets `EPOLL_CTL_DEL` **twice** — once in `_unregisterEventFd` and once in `closeConnection`. This is harmless (second call returns ENOENT) but wasteful and indicates a design inconsistency.

### ISSUE 3: `ConnectionManager::addConnection` — `new Connection` not exception-safe

**File**: `ConnectionManager.cpp:100`

```cpp
Connection* conn = new Connection(client_fd, config);
```

If `new` throws `std::bad_alloc`, `client_fd` is never closed. The commented-out try-catch below (lines 102-109) was apparently meant to handle this but was never enabled.

---

## 5. MINOR ISSUES

| File | Line | Issue |
|------|------|-------|
|✅ `ResponseHandler.cpp` | 36 | Comment says `unlink, getpid` but `unlink` was replaced with `std::remove` — stale comment |
|✅ `server_parser.cpp` | 25 | `atoi()` called without `std::` prefix — use `safe_strtol` for consistency |
| `ResponseHandler.cpp` | 640 | `::getpid()` — **forbidden function** (see violations section) |
| `main.cpp` | 58 | `::inet_addr()` — **forbidden function** (see violations section) |
| `Connection.cpp` | 106 | Extra whitespace before `size_t server_default` |

---

## 6. SUMMARY

### Must Fix (will cause grade 0 or server crash):

1. **`gettimeofday` in Logger.hpp** — forbidden function, 3 call sites
2. **`::inet_addr` in main.cpp** — forbidden function
3. **`::getpid` in ResponseHandler.cpp** — forbidden function
4. **Uncaught exception from `_startCgi`** — server crash on `std::bad_alloc` or epoll failure during CGI start

### Should Fix (correctness / robustness):

5. **Signal handler calling non-async-signal-safe functions** — UB on signal during map operation
6. **`result_write_fd` leak** in `_startCgi` catch block
7. **`_registerEventFd` EventRef leak** on `epoll_ctl` failure
8. **`::gmtime`/`::strftime` in Logger.hpp** — use `std::` prefix for safety

### Nice to Fix:

9. `atoi` → `safe_strtol` in `✅ server_parser.cpp `, `main.cpp`, `ConnectionManager.cpp`
10. Copy `gmtime` result immediately to avoid static-pointer issues
11. Handle `send() == 0` in `_handleWrite`
12. Check `_setCloexec` return on epoll_fd
