# [x] Problem 1: Buffer-based check is ineffective for request bodies
Location: src/Buffer.cpp lines 26-33
Explanation: Buffer checks current size, not total body size; parser drains buffer continuously
Impact: Large uploads never trigger 413
# [x] Problem 2: Connection uses server-level cmbs, not location-specific
Location: src/Connection.cpp lines 24-28
Explanation: Connection created before request path known, uses server-level limit
Impact: Location-specific limits (e.g., 30MB for /cgi-bin/) are ignored
# [x] Problem 3: HttpParser doesn't check total body size
Location: src/HttpParser.cpp lines 653-667 (_parseBody) and 691-790 (_parseChunked)
Explanation: Parser appends to req.body without checking against location limit
Impact: No enforcement during parsing, client can send unlimited body
# [x] Problem 4: CgiJob uses wrong limit (server-level, not location-specific)
Location: src/EventLoop.cpp line 222
Explanation: Uses conn->writeBuffer().maxSize() which is server-level (10MB)
Impact: CGI output limited to 10MB instead of location-specific 30MB
# [x] Problem 5: Upload handling doesn't check body size
Location: src/ResponseHandler.cpp lines 281-286
Explanation: _handlePost processes uploads without checking req.body.size()
Impact: Uploads can exceed location limits
# [x] Problem 6: No body size tracking in HttpRequest
Location: Headers/HttpRequest.hpp lines 66-67
Explanation: No field to track location-specific limit for this request
Impact: No way to enforce location-specific limit during parsing

# [x] 1. Bug 1: _serveStaticFile returns 500 for missing files (should be 404)
   - Location: src/ResponseHandler.cpp lines 452-456
   - Current behavior: Returns 500 Internal Server Error when file doesn't exist (ENOENT)
   - Correct behavior: Should return 404 Not Found for ENOENT, 403 Forbidden for EACCES, 500 for other errors
   - Reason: File not found is a client error (4xx), not a server error (5xx)

# [x] 2. Bug 2: _sendDirectoryListing returns 403 for missing directories (should be 404)
   - Location: src/ResponseHandler.cpp lines 500-504
   - Current behavior: Returns 403 Forbidden when directory doesn't exist (ENOENT)
   - Correct behavior: Should return 404 Not Found for ENOENT, 403 Forbidden for EACCES, 500 for other errors
   - Reason: Directory not found is a client error (4xx), not a permission issue

# [x] 3. Bug 3: _handleDelete returns 500 for ENOENT (should be 404)
   - Location: src/ResponseHandler.cpp lines 695-700
   - Current behavior: Returns 500 Internal Server Error when file doesn't exist (ENOENT)
   - Correct behavior: Should return 404 Not Found for ENOENT (or 204 No Content), 403 Forbidden for EACCES/EPERM, 500 for other errors
   - Reason: File not found is a client error (4xx), not a server error

Include a summary table showing the pattern: ENOENT errors are incorrectly treated as server errors (500) instead of client errors (404).


# [] --------------------------------------------------------------------------
# [] 1. Bug 1: Memory Leak in _startCgi on CGI Failure
   - Location: src/EventLoop.cpp lines 222-260
   - Problem: If _addCgiFd throws due to epoll_ctl failure, CgiJob is already in _cgi_jobs map but never cleaned up
   - Severity: HIGH
   - Fix: Move map insertion after epoll registration succeeds, or wrap in try-catch

# [] 2. Bug 2: File Descriptor Leak in _startCgi on setNonBlocking Failure
   - Location: src/EventLoop.cpp lines 212-220
   - Problem: If _rearmClient throws, connection state is corrupted but fds are already closed
   - Severity: MEDIUM
   - Fix: Wrap _rearmClient in try-catch or ensure consistent error handling

# [x] 3. Bug 3: Integer Overflow in HTTP Version Parsing
   - Location: src/HttpParser.cpp lines 420-424
   - Problem: No overflow check on http_minor, malicious client can cause undefined behavior
   - Severity: MEDIUM
   - Fix: Add overflow check: if (http_minor > 99) { req.parse_state = PSTATE_ERROR; req.error_code = 400; return; }

# [x] 4. Bug 4: Integer Overflow in Chunk Size Parsing
   - Location: src/HttpParser.cpp lines 723-740
   - Problem: No overflow check on chunk_sz, malicious client can bypass body size limits
   - Severity: HIGH
   - Fix: Add overflow check: if (chunk_sz > SIZE_MAX / 16) { req.parse_state = PSTATE_ERROR; req.error_code = 400; return; }

# [] 5. Bug 5: Race Condition in sig_handler
   - Location: src/main.cpp lines 32-36
   - Problem: Signal handler is async-signal-unsafe, calling g_loop->stop() is undefined behavior
   - Severity: HIGH
   - Fix: Use volatile sig_atomic_t for flag or use signalfd/eventfd for signal handling

# [x] 1. Bug 6: TOCTOU Race Condition in CGI Handler
   - Location: cgi/CgiHandler.cpp lines 223-267
   - Problem: stat() and access() called before execve(), attacker can replace files between check and use
   - Severity: HIGH
   - Impact: Arbitrary code execution
   - Fix: Use open() with O_NOFOLLOW and fexecve(), or validate inode after fork

# [x] 2. Bug 7: Integer Overflow in Content-Length Parsing
   - Location: src/HttpParser.cpp line 627
   - Problem: No overflow check when parsing Content-Length header
   - Severity: HIGH
   - Impact: Bypass body size limits, memory corruption
   - Fix: Add overflow check: if (cl > SIZE_MAX / 10) { req.parse_state = PSTATE_ERROR; req.error_code = 400; return; }

# [c] 3. Bug 8: TOCTOU in File Serving
   - Location: src/ResponseHandler.cpp lines 452-465
   - Problem: open() called then fstat(), file could be replaced between operations
   - Severity: MEDIUM
   - Impact: Serve wrong content, crash
   - Fix: Use fstat() immediately after open(), or use openat() with directory fd

# [c] 4. Bug 9: Symlink Attack in Autoindex
   - Location: src/ResponseHandler.cpp lines 534-544
   - Problem: stat() follows symlinks without validation in autoindex
   - Severity: HIGH
   - Impact: Information disclosure, file access
   - Fix: Use lstat() to detect symlinks, or validate path with realpath()

# [x] 5. Bug 10: Resource Exhaustion - Unbounded Pending Reap List
   - Location: src/EventLoop.cpp lines 453-481
   - Problem: _pending_reap vector can grow unbounded with stuck CGI processes
   - Severity: MEDIUM
   - Impact: Memory exhaustion DoS
   - Fix: Add hard limit on _pending_reap size (e.g., 1000 entries)

# [x] 6. Bug 11: Missing Error Handling in _modifyEventFd
   - Location: src/EventLoop.cpp lines 115-127
   - Problem: epoll_ctl MOD failure only logs, connection hangs indefinitely
   - Severity: MEDIUM
   - Impact: Connection hangs
   - Fix: Close connection on MOD failure

# [c] 7. Bug 12: Double Close Risk in CGI stdin_fd
   - Location: src/EventLoop.cpp lines 265-278
   - Problem: _closeCgiStdin() called from multiple places, race condition between check and close
   - Severity: LOW
   - Impact: Could close wrong fd if fd is reused
   - Fix: Use atomic operations or ensure single ownership

# [] 1. Bug 13: FD_CLOEXEC Failure Ignored on epoll_fd
   - Location: src/EventLoop.cpp line 59
   - Problem: If FD_CLOEXEC fails to set on epoll fd, it just logs warning but continues
   - Severity: MEDIUM
   - Impact: epoll fd leaks to CGI child processes, causing event loop corruption
   - Fix: If FD_CLOEXEC fails, close the fd and throw an exception

# [c] 2. Bug 14: Memory Leak on EventRef Allocation Failure
   - Location: src/EventLoop.cpp line 91
   - Problem: If new EventRef throws std::bad_alloc, the fd is not cleaned up
   - Severity: LOW
   - Impact: fd remains open but untracked
   - Fix: Wrap in try-catch or use std::nothrow

# [c] 3. Bug 15: result_write_fd Always Closed Even on CGI Failure
   - Location: src/EventLoop.cpp line 252
   - Problem: result_write_fd closed unconditionally, even if CGI start fails
   - Severity: MEDIUM
   - Impact: CGI child may get SIGPIPE when writing
   - Fix: Move close inside if (ok) block, or close after killing child in error path

# [] 4. Bug 16: _cgi_stdin_jobs Erase Before Unregister
   - Location: src/EventLoop.cpp lines 271-272
   - Problem: Map erase happens before _unregisterEventFd, which could throw
   - Severity: MEDIUM
   - Impact: Orphaned fd if epoll_ctl fails
   - Fix: Call _unregisterEventFd first, then erase from map

# [c] 5. Bug 17: Hardcoded CGI Timeout
   - Location: src/EventLoop.cpp line 491
   - Problem: CGI timeout hardcoded to 10 seconds, not configurable
   - Severity: LOW
   - Impact: Too short for long-running CGI scripts
   - Fix: Make timeout configurable in server config

# [] 6. Bug 18: Integer Overflow in Buffer Size Check
   - Location: src/Buffer.cpp line 31
   - Problem: If _head > _storage.size(), subtraction underflows, bypassing check
   - Severity: LOW
   - Impact: Bypass limit if buffer corrupted
   - Fix: Add check: if (_head > _storage.size()) { /* error */ }

# [c] 7. Bug 19: ConnectionManager::rearmEpoll Throws Instead of Handling Error
   - Location: src/ConnectionManager.cpp lines 132-135
   - Problem: Throws exception on epoll_ctl MOD failure, can crash server
   - Severity: HIGH
   - Impact: Server crash on single connection failure
   - Fix: Replace throw with error logging and close connection

# [c] 8. Bug 20: No Try-Catch Around EventLoop Constructor in main
   - Location: src/main.cpp line 94
   - Problem: EventLoop constructor can throw, not wrapped in try-catch
   - Severity: HIGH
   - Impact: Improper shutdown on initialization failure
   - Fix: Wrap EventLoop construction and main logic in try-catch