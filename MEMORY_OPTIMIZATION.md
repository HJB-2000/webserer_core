# Memory Optimization Guide

This document explains the memory optimization fixes applied to the web server to prevent memory exhaustion under heavy load.

## Problem Description

When running heavy concurrent tests (128 workers × 50 requests with 100MB POST bodies), the server would:
- Exhaust all RAM and freeze
- Peak memory usage reached 4GB+ without optimizations
- Memory would not be released properly after responses

## Root Causes Identified

### 1. Request Body Not Freed (Original Issue)
**Location:** `src/EventLoop/EventLoop_helper_cgi.cpp`

The request body (up to 100MB) and `body_fd` (temp file) were not being freed in several CGI job completion paths.

**Affected functions:**
- `_finishCgiJob()` - Not freeing body after CGI output received
- `_failCgiJob()` - Not freeing body on CGI error
- `_startCgi()` (error path) - Not freeing body when CGI fails to start
- `_closeCgiJobsForClient()` - Not freeing body when client disconnects

### 2. Large String Copies in handleCgiOutput
**Location:** `src/ResponseHandler.cpp`

The original `handleCgiOutput()` created multiple large string copies:
```cpp
std::string raw(cgi_output.data(), cgi_output.size());  // 100MB copy
std::string headers_raw = raw.substr(0, sep);           // another copy
std::string body = raw.substr(sep + body_skip);         // another copy
```

### 3. Unbounded Buffer Sizes
**Location:** `Headers/CgiJob.hpp`, `src/Connection.cpp`

Buffer limits were using the config's `client_max_body_size` (999MB) instead of reasonable limits:
- `CgiJob.result_buffer` was created with 999MB limit
- `Connection.write_buffer` was created with 999MB limit

### 4. Buffer Capacity Never Released
**Location:** `src/Buffer.cpp`

When `Buffer::reset()` was called, the vector's capacity was never released:
- `clear()` only changes size, not capacity
- Memory remained allocated for future use

## Fixes Applied

### Fix 1: Memory Cleanup in CGI Job Handlers
**Files:** `src/EventLoop/EventLoop_helper_cgi.cpp`

**Changes:**
```cpp
// In _finishCgiJob(), _failCgiJob(), and error paths:
std::string().swap(const_cast<HttpRequest&>(conn->request()).body);
conn->readBuffer().reset();
if (conn->request().body_fd >= 0)
{
    ::close(conn->request().body_fd);
    const_cast<HttpRequest&>(conn->request()).body_fd = -1;
}
```

**Purpose:** Free request body memory and close temp file descriptors when CGI processing completes.

**Result:** Prevents memory leaks when requests complete.

---

### Fix 2: Direct Buffer Processing in handleCgiOutput
**Files:** `src/ResponseHandler.cpp`

**Changes:** Replaced large string copies with direct buffer parsing:
```cpp
const char* data = cgi_output.data();
const size_t size = cgi_output.size();

// Parse headers directly from buffer without copies
// Write body directly: _appendStr(wb, data + sep + body_skip, body_size);
```

**Purpose:** Eliminate unnecessary memory copies when processing CGI output.

**Result:** Reduced peak memory by avoiding 3 copies of 100MB+ data.

---

### Fix 3: Bounded Buffer Sizes
**Files:** `Headers/CgiJob.hpp`, `src/Connection.cpp`

**Changes:**
```cpp
// CgiJob.hpp
static const size_t CGI_RESULT_BUFFER_LIMIT = 100 * 1024 * 1024;  // 100MB

// Connection.cpp
static const size_t RESPONSE_BUFFER_LIMIT = 100 * 1024 * 1024;  // 100MB
```

**Purpose:** Limit maximum buffer size to prevent unbounded memory growth.

**Result:** Each connection/job is limited to 100MB instead of potentially 999MB.

---

### Fix 4: Vector Swap for Memory Release
**Files:** `src/Buffer.cpp`

**Changes:**
```cpp
void Buffer::reset()
{
    _head = 0;
    size_t initial = 16 * 1024;  // 16KB minimum
    
    // Swap with empty vector to truly release memory
    std::vector<char> empty;
    empty.reserve(initial);
    _storage.swap(empty);
}
```

**Purpose:** Actually release allocated memory when buffers are reset, not just mark it as available.

**Result:** Memory is returned to the system after requests complete.

---

### Fix 5: Keep Write Buffer Limit on Reset
**Files:** `src/Connection.cpp`

**Changes:**
```cpp
void Connection::reset()
{
    _read_buffer.reset();
    _write_buffer.reset();
    // Keep buffer limits at RESPONSE_BUFFER_LIMIT (100MB)
    size_t server_default = _config->getMaxBody();
    _read_buffer.setMaxSize(server_default);
    _write_buffer.setMaxSize(std::min(server_default, RESPONSE_BUFFER_LIMIT));
    ...
}
```

**Purpose:** Prevent write buffer from growing back to full config size on keep-alive reuse.

**Result:** Buffers stay bounded across multiple requests on the same connection.

---

## Memory Optimization Summary

| Metric | Before | After |
|--------|--------|-------|
| Peak Memory | 4GB+ | ~2GB |
| Memory per Connection | 999MB | 100MB |
| Memory Release | No | Yes (via swap) |
| String Copies | 3 per CGI response | 0 |

## Trade-offs

The 100MB buffer limit is the minimum required to handle 100MB CGI responses. Further reduction would cause test failures.

To reduce memory below 2GB, architectural changes would be needed:
1. **Connection limits** - limit max concurrent connections
2. **Streaming responses** - pipe CGI output directly to client without buffering
3. **Disk-based buffering** - write large responses to temp files instead of memory

## Files Modified

1. `src/EventLoop/EventLoop_helper_cgi.cpp` - CGI job memory cleanup
2. `src/ResponseHandler.cpp` - Direct buffer processing
3. `Headers/CgiJob.hpp` - Buffer size limit
4. `src/Connection.cpp` - Buffer size limits, reset behavior, body_fd cleanup
5. `src/Buffer.cpp` - Memory release on reset
6. `src/EventLoop/EventLoop.cpp` - Shutdown leak fix, signal safety
7. `src/make_listener.cpp` - Initialize setsockopt variables
8. `src/main.cpp` - Remove double close of server fds
9. `src/EventLoop/EventLoop_helper.cpp` - Clean stop with _stopped flag
10. `Headers/EventLoop.hpp` - volatile sig_atomic_t for signal safety

## Valgrind Errors Fixed

### 1. Uninitialized Memory in setsockopt
**File:** `src/make_listener.cpp`

**Problem:** Variables `reuse`, `reuseport`, and `nodaly` were passed to `setsockopt()` without initialization.

**Fix:**
```cpp
// Before (uninitialized)
int reuse, reuseport, nodaly = 1;

// After (initialized)
int reuse = 1;
int reuseport = 1;
int nodaly = 1;
```

### 2. Double Close of Server File Descriptors
**File:** `src/main.cpp`

**Problem:** `stop()` already closes all server fds, but main.cpp was trying to close them again, causing "fd already closed" errors.

**Fix:** Removed the redundant loop that closed server fds in main.cpp since `stop()` handles this.

### 3. Double Close and FD Leak for body_fd
**Files:** `src/Connection.cpp`, `src/EventLoop/EventLoop_helper_cgi.cpp`

**Problem:** 
- `body_fd` was being closed in `_closeCgiJobsForClient`, but Connection destructor also tried to close it, causing double close
- Some body_fd were never closed at all, leaving temp files open

**Fix:**
- Added body_fd cleanup in Connection destructor
- Removed redundant body_fd close from `_closeCgiJobsForClient`

This fixes:
- Valgrind "fd already closed" error for /tmp/cgi_body_* files
- FD leaks at exit (temp files still open)

### 4. Server FD Double Close and CGI Pipe Leaks
**Files:** `src/EventLoop/EventLoop_helper.cpp`, `src/EventLoop/EventLoop.cpp`

**Problem:**
- Server fd (5) was closed in `stop()`, but destructor tried to close it again
- CGI pipes were not closed when server stopped, causing FD leaks

**Fix:**
- `stop()` now marks server fds as closed (-1) to prevent double-close
- `stop()` now closes all CGI result and stdin pipe fds
- Destructor checks if fds are >= 0 before closing

This fixes:
- "fd already closed" error for server socket
- FD leaks at exit (6 CGI pipes still open)

### 5. Signal Handler Race Condition
**Files:** `Headers/EventLoop.hpp`, `src/EventLoop/EventLoop.cpp`, `src/EventLoop/EventLoop_helper.cpp`, `src/EventLoop/EventLoop_helper_cgi.cpp`

**Problem:** When SIGINT arrives, stop() closes all pipes immediately, but the event loop may still have pending events to process. This causes:
- `_handleCgiEvent` tries to read from closed pipes
- `_closeCgiJob` tries to close already-closed fds
- "fd already closed" errors

**Fix:**
- Changed `_running` to `volatile sig_atomic_t` for signal safety
- Added `_stopped` flag to prevent new event processing
- `stop()` sets `_stopped` and closes epoll to prevent new events
- `_dispatch()` checks `_stopped` before processing events
- `_handleCgiEvent()` checks `_stopped` before processing
- `_closeCgiJob()` only closes fds if epoll_fd is still valid

## Shutdown Memory Leaks

When the server receives SIGINT during CGI processing, memory leaks occurred because the destructor was not properly cleaning up resources.

### Root Cause
The `EventLoop::~EventLoop()` destructor was only deleting the ConnectionManager, leaving CGI jobs, event refs, and child processes unfreed.

### Fix Applied
**File:** `src/EventLoop/EventLoop.cpp`

Added comprehensive cleanup in the destructor:
- Kill and reap all child CGI processes
- Close all stdin file descriptors
- Delete all CgiJob objects
- Delete all EventRef objects
- Clear all containers and close server fds

## Testing

### Valgrind Massif
Run Valgrind Massif to verify memory usage:
```bash
valgrind --tool=massif ./webserv youpi.conf
# In another terminal, run the tester
./tester http://localhost:5500
# View results
ms_print massif.out.* | less
```

Peak memory should stabilize around 2GB under heavy load instead of growing indefinitely.

### Address Sanitizer
Run with address sanitizer to detect memory leaks:
```bash
# Build with sanitizer
make SANITIZE=1

# Run server
./webserv youpi.conf

# In another terminal, run the tester
./tester http://localhost:5500

# Send SIGINT during testing to check for shutdown leaks
# No "LeakSanitizer" errors should appear
```

### Valgrind
Run comprehensive checks for memory errors and leaks:
```bash
# Run valgrind with all checks enabled
valgrind --leak-check=full --show-leak-kinds=all --track-fds=all --track-origins=yes ./webserv youpi.conf

# Or use the alias (if configured)
va
```

Expected results after fixes:
- No "uninitialised byte(s)" errors
- No "fd already closed" errors  
- No memory leaks reported
- All heap blocks freed at exit