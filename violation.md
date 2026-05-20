## Overview
This report identifies all functions used in the codebase that **violate** the "External Functions" rule defined in `subject.md` (Chapter IV, Mandatory part).
**Rule:** Only the explicitly listed functions are allowed. Any function not on the list is **forbidden**.
### ✅ Allowed External Functions List
`execve`, `pipe`, `strerror`, `gai_strerror`, `errno`, `dup`, `dup2`, `fork`, `socketpair`, `htons`, `htonl`, `ntohs`, `ntohl`, `select`, `poll`, `epoll` (`epoll_create`, `epoll_ctl`, `epoll_wait`), `kqueue` (`kqueue`, `kevent`), `socket`, `accept`, `listen`, `send`, `recv`, `chdir`, `bind`, `connect`, `getaddrinfo`, `freeaddrinfo`, `setsockopt`, `getsockname`, `getprotobyname`, `fcntl`, `close`, `read`, `write`, `waitpid`, `kill`, `signal`, `access`, `stat`, `open`, `opendir`, `readdir`, `closedir`.
---
## 🚫 Violations Found
| # | Forbidden Function | File | Line(s) | Reason |
|---|--------------------|------|---------|--------|
| 1 | `✅ gettimeofday` | `cgi/CgiHandler.cpp` | 157, 263 | Not in allowed list |
| 2 | `✅gmtime->std::gmtime` | `src/ResponseHandler.cpp` | 914 | basic C standard library functions from <ctime>|
| 3 | `✅strftime->std::strftime`|`src/ResponseHandler.cpp` | 916 |basic C standard library functions from <ctime> |
| 4 | `✅ pipe2` | `cgi/CgiHandler.cpp` | 228 | Only `pipe` is allowed, `pipe2` is distinct |
| 5 | `✅ pipe2` | `src/EventLoop.cpp` | 167 | Only `pipe` is allowed, `pipe2` is distinct |
| 6 | `✅ getcwd` | `cgi/CgiHandler.cpp` | 308 | Not in allowed list |
| 7 | `✅ unlink -> remove` | `src/ResponseHandler.cpp` | 809 | Not in allowed list |
| 8 | `✅fstat` | `src/ResponseHandler.cpp` | 684 | Not in allowed list (only `stat` is listed) |
| 9 | `✅ strtod` | `conf/parserConf.cpp` | 162 | Not in allowed list |
| 10 |`✅ strtol` | `conf/httpConfig.cpp` | 25 | Not in allowed list |
---
## 📁 Detailed Breakdown by File
### 1. `cgi/CgiHandler.cpp`
- **Line 157**: `gettimeofday(&start, NULL);`
  - ✅ **Violation**: `gettimeofday` is not in the allowed list.
- **Line 228**: `if (pipe2(pipefd, O_NONBLOCK) == -1)`
  - ✅ **Violation**: `pipe2` is not in the allowed list. Only `pipe` is permitted.
- **Line 263**: `gettimeofday(&end, NULL);`
  - ✅ **Violation**: `gettimeofday` is not in the allowed list.
- **Line 308**: `char cwd[1024]; getcwd(cwd, sizeof(cwd));`
  - ✅ **Violation**: `getcwd` is not in the allowed list.
### 2. `src/ResponseHandler.cpp`
- **Line 684**: `fstat(fd, &st);`
  - ✅ **Violation**: `fstat` is not in the allowed list. Only `stat` is permitted.
- **Line 809**: `unlink(path.c_str());`
  - ✅ **Violation**: `unlink` is not in the allowed list.
- **Line 914**: `tm *timeinfo = gmtime(&rawtime);`
  - ✅ **Violation**: `gmtime` is not in the allowed list.
- **Line 916**: `strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", timeinfo);`
  - ✅ **Violation**: `strftime` is not in the allowed list.
### 3. `src/EventLoop.cpp`
- **Line 167**: `pipe2(_shutdown_fd, O_NONBLOCK);`
  - ✅ **Violation**: `pipe2` is not in the allowed list. Only `pipe` is permitted.
### 4. `conf/parserConf.cpp`
- **Line 162**: `double val = strtod(token, &endptr);`
  - ✅ **Violation**: `strtod` is not in the allowed list.
### 5. `conf/httpConfig.cpp`
- **Line 25**: `long val = strtol(token, &endptr, 10);`
  - ✅ **Violation**: `strtol` is not in the allowed list.
---
## 💡 Recommended Fixes
| Forbidden Function | Suggested Alternative / Fix |
|--------------------|-----------------------------|
| `✅ gettimeofday` | Use `clock_gettime` (if allowed in future) or track time via allowed mechanisms. If strictly forbidden, remove timing logic. |
| `gmtime`, `strftime` | Manually format time strings or implement a minimal internal formatter using basic arithmetic on `time_t` (if `time` becomes allowed) or remove date headers. |
| ✅ `pipe2` | Replace with `pipe()` followed by `fcntl(fd, F_SETFL, O_NONBLOCK)` for each fd. |
| ✅ `getcwd` | Track working directory internally or remove dependency on current path. |
| `unlink` | Remove file deletion logic or implement via allowed system calls if possible (usually requires `unlink`). |
| `✅fstat` | Replace with `stat()` by passing the file path instead of the file descriptor. |
|`✅strtod`, ` ✅strtol`| Implement custom string-to-number parsing functions using only allowed basic operations. |
---
## 📊 Summary
- **Total Violations**: 10
- **Files Affected**: 5
- **Unique Forbidden Functions**: 8 (`gettimeofday`, `gmtime`, `strftime`, `pipe2`, `getcwd`, `unlink`, `fstat`, `strtod`, `strtol`)
**Action Required**: All listed functions must be replaced or removed to comply with the subject requirements.


Logger.hpp (strict, per your request)
You said: “if you see an implementation of any function inside the class in the header file report it too be strict.”

Inline implementations inside Logger.hpp
There are many function bodies defined inside the header, including:

TeeStreambuf::overflow, xsputn, _utcTimestamp
Logger::instance, open, close, setLevel, getLevel, log, perf, state, path
Logger::Stopwatch constructor, elapsed_ms, str, reset
Logger::_writeTagged, _levelTag
So Logger.hpp is full of inline implementations, which violates the strict “no implementation in headers” requirement.

Forbidden functions still used inside Logger.hpp
gettimeofday (for Stopwatch)
gmtime + strftime (for timestamp formatting)
Those also remain in the header.
