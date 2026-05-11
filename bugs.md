Comprehensive Bug Report - 42 Webserv Project
Executive Summary
After deep analysis of the webserv implementation against the 42 subject requirements, I've identified 23 bugs categorized by severity. The most critical issues involve out-of-bounds memory access, incorrect HTTP header validation, and protocol violations.

## Recent Fixes (May 11, 2026)
- Hardened session parsing in `www/html/cgi-bin/toydb.py` to extract usernames from dict-based sessions.
- Improved `www/html/cgi-bin/upload_file.py` robustness: safe CONTENT_LENGTH parsing, boundary parsing, and HTML-escaped output/error messages.
- Updated `www/html/errors/413.html` and `www/html/test-413.html` to reflect webserv and configured body limits (10MB global, 30MB for /cgi-bin).
- Cleaned ResponseHandler CGI comment to remove personal notes.
- Removed legacy commented-out code in `www/html/cgi-bin/dashboard.py`.
- Disabled the `www/html/cgi-bin/infinite.py` endpoint to avoid remote DoS risk.
- Corrected `error_page` parser error messages to reference the http context.
- Updated `conf/parsing.hpp` comments to reflect events parsing.
- Removed the unused `printer_of_conf_parser()` declaration from `conf/parserConf.hpp`.
- Mapped CGI body-limit exceptions to 413 in `src/EventLoop.cpp`.
# CRITICAL SEVERITY (Crash/UB/Security)
##✅ BUG-C1: Out-of-Bounds Array Access in return Directive Parser
File: conf/location_parser.cpp, Line 133
Issue: values[1] accessed without checking values.size() >= 2
Exploit: Config with return 301; (missing URL) causes OOB read → crash/garbage
Fix: Add if (values.size() < 2) report_parse_error(...) before accessing values[1]

##✅ BUG-C2: Missing _index_Files Copy in Assignment Operator
File: conf/serverConfig.cpp, Lines 84-98
Issue: operator= copies all members BUT _index_Files is mysteriously present (actually it IS copied at line 93). However, the copy semantics need verification for shallow vs deep copy.
Note: On re-examination, _index_Files IS being copied. This may be a false positive in error.md.
##✅ BUG-C3: is_valid_number("") Returns True (Empty String Validation Bypass)
File: conf/parserConf.cpp, Lines 67-75
Issue: Empty string loop never executes, returns true
Impact: is_valid_host() accepts malformed IPs like 1..2.3, .1.2.3, 1.2.3.
Fix: Add if (str.empty()) return false; at function start
## BUG-C4: No Duplicate Header Detection (HTTP Request Smuggling Vector)
File: src/HttpParser.cpp, Line 546
Issue: req.headers[name] = value; silently overwrites duplicate headers
RFC Violation: RFC 7230 §3.2.2 requires specific handling for duplicate headers:
Host, Content-Length, Transfer-Encoding MUST reject duplicates → 400 Bad Request
Other headers should concatenate or reject based on semantics
Exploit:
http
12345
Server accepts last value, backend may interpret first → desync attack
Fix: Implement duplicate detection for critical headers with strict rejection
## BUG-C5: Both Content-Length AND Transfer-Encoding Accepted Simultaneously
File: src/HttpParser.cpp, Lines 573-598
Issue: Parser processes both headers without mutual exclusion
RFC Violation: RFC 7230 §3.3.3 states if both are present, server MUST reject with 400
Exploit: Classic request smuggling vector
Fix: If both headers present → return 400 Bad Request immediately
## BUG-C6: Path Traversal Check Incomplete
File: src/ResponseHandler.cpp, Lines 98-107
Issue: Only checks for /../ but misses:
%2e%2e%2f (URL-encoded)
.. as complete path
Windows-style \..\
**Exploit:** GET /static/%2e%2e%2fetc/passwd HTTP/1.1 bypasses check
**Fix:** Decode URL first, then check for .. components in normalized path
# HIGH SEVERITY
## ✅BUG-H1: Location Prefix Match Without Boundary Check
File: conf/serverConfig.cpp, Lines 29-48
Issue: path.find(loc_path) == 0 matches /api against /apidoc
Expected: Next char must be / or end-of-string
Fix: Add boundary check: if (path.length() > loc_path.length() && path[loc_path.length()] != '/') continue;
## ✅BUG-H2: Configuration Order Dependency
File: conf/locationConfig.cpp + conf/server_parser.cpp
Issue: Location inherits Server state at parse time. If location / appears before root directive → validation fails
Impact: Semantically valid configs rejected due to directive ordering
Fix: Two-pass parsing: collect directives first, validate after full block parsed
## ✅BUG-H3: Comment Parsing Breaks Valid Paths
File: conf/parsing.cpp, Lines 5-27
Issue: # anywhere treated as comment start, even in quoted strings or paths
Example: root /var/www#backup; truncates to root /var/www
Fix: Only treat # as comment when it starts a token (after whitespace)
## ✅BUG-H4: Signed Char UB in isdigit()/isspace() Calls
File: Multiple files (parsing.cpp:36-39, parserConf.cpp:69-72)
Issue: Passing raw char to isdigit() is UB for non-ASCII bytes
Fix: Cast to unsigned char: isdigit(static_cast<unsigned char>(ch))
## ✅BUG-H5: client_max_body_size Cap Inconsistent Across Levels
Files: parserConf.cpp:84-92, server_parser.cpp:208-214, location_parser.cpp:106-112
Issue: HTTP/server levels cap at 1 GiB, location level has NO cap
Subject Requirement: Maximum 1 GiB everywhere
Fix: Apply same cap in location_parser.cpp::parseDirective()
## ✅BUG-H6: Host Header Port Not Stripped in Virtual Host Matching
File: conf/serverConfig.cpp, Lines 7-27
Issue: Host: example.com:8080 doesn't match server_name example.com
Impact: Virtual hosting broken when client includes explicit port
Fix: Strip :port suffix from Host header before comparison
## ✅BUG-H7: timeout 0; Accepted Then Rejected
File: server_parser.cpp, Lines 215-226 + 308-309
Issue: Setter allows 0, validator rejects ≤0 → misleading "Missing required directive" error
Fix: Either forbid 0 in setter OR change validator message
MEDIUM SEVERITY
## BUG-M1: Chunked Encoding Extensions Not Properly Handled
File: src/HttpParser.cpp, Lines 650-680
Issue: Chunk extensions after ; are ignored but not validated
RFC Requirement: Extensions must be parsed (even if ignored) per RFC 7230 §4.1.1
Risk: Malformed chunked requests may be accepted
## BUG-M2: HTTP/0.9 Detection Logic Flawed
File: src/HttpParser.cpp, Lines 356-358
Issue: URI followed directly by \r or \n sets http09 = true
Problem: Could misinterpret malformed HTTP/1.x as 0.9
Current Behavior: Returns 505 (correct), but detection logic is fragile
## BUG-M3: Method Validation Only in Location Context
File: src/ResponseHandler.cpp, Lines 113-118
Issue: Method allowed check only runs if loc exists
Gap: Requests matching no location skip method validation entirely
Fix: Apply method check at server level too
## BUG-M4: Autoindex Directory Listing XSS Protected But Path Not Normalized
File: src/ResponseHandler.cpp, Lines 428-501
Issue: htmlEscape() applied but directory symlinks could escape root
Fix: Verify resolved realpath stays within server root
## BUG-M5: DELETE Handler Lacks Confirmation/CSRF Protection
File: src/ResponseHandler.cpp, _handleDelete()
Issue: DELETE requests processed without any origin/safety checks
Note: Per subject, this may be acceptable, but worth documenting
## BUG-M6: CGI Timeout Hardcoded to 10 Seconds
File: src/EventLoop.cpp, _closeTimedOutCgiJobs()
Issue: No config option to adjust CGI timeout
Subject: Should be configurable per location
## ✅BUG-M7: Error Page Paths With Semicolons
File: conf/serverConfig.cpp:258-259, conf/locationConfig.cpp multiple lines
Issue: Default error pages stored as "./errors/400.html;" (semicolon included)
Impact: open() fails, falls back to built-in HTML
Fix: Remove trailing ; from string literals
LOW SEVERITY
## ✅BUG-L1: Duplicate Methods in allowed_methods Accepted
File: conf/location_parser.cpp, Lines 77-86
Issue: allowed_methods GET GET POST; silently accepted
## ⚠️BUG-L2: Extra Values Ignored for Scalar Directives
File: server_parser.cpp:195-203 (root), location_parser.cpp:87-93 (autoindex)
Issue: root a b c; uses only first value, ignores extras without warning
## ✅BUG-L3: atoi() Overflow Not Handled
File: Multiple locations (listen port, error codes, return codes)
Issue: Very large numbers pass is_valid_number() but overflow atoi()
Fix: Use strtol() with ERANGE check (already done in parse_cl_mx_bd_sz)
## ✅BUG-L4: server_name Required But Subject Doesn't Mandate It
File: server_parser.cpp:304-305
Issue: Forces server_name even for single-server configs
## ✅BUG-L5: Lexer Keywords Collide With Valid Location Paths
File: location_parser.cpp:10-16
Issue: location server { } rejected because server lexed as TYPE_CONTEXT
## ✅BUG-L6: cgi_path/cgi_pass Treated as Distinct But Deduped Together
File: location_parser.cpp:28-33 + 94-105
Issue: Can specify both, second overwrites first without error
## BUG-L7: client_max_body_size 0 Means "Reject All" Not "Unlimited"
File: Buffer.cpp logic
Issue: Nginx semantics: 0 = unlimited. Here: 0 = reject everything
Fix: Document or change behavior
## ✅BUG-L8: Dead Code - I_Want() and check_dup_path_locations() Declared But Undefined
Files: conf/parserConf.hpp:35, conf/serverConfig.hpp:45
Issue: Linker error if ever called
## ✅BUG-L9: using namespace std; in Headers
Files: Multiple .hpp files in conf/
Issue: Pollutes global namespace, violates coding standards

## 🚨 BUG-C7: Missing client_max_body_size Enforcement
test ➜ 
webserer_core git:(master) ✗ curl http://127.0.0.1:8080/cgi-bin/big.py
curl: (52) Empty reply from server
output ➜
terminate called after throwing an instance of 'BodyLimitException'
  what():  Buffer: client_max_body_size exceeded -> 413
[1]    22669 IOT instruction  ./webserv fahd.conf
## explanation and fix 
Here's exactly how to trigger bug #6:
The bug: _handleCgiEvent calls job->result_buffer.append(...) which throws BodyLimitException when the CGI output exceeds client_max_body_size. That exception is not caught in _handleCgiEvent, so it propagates up through _dispatch → run() → crashes the server.
How to trigger it with your config:
The CGI location has client_max_body_size 30M. But the CgiJob is initialized with:
cppCgiJob* job = new CgiJob(conn->fd(), result_read_fd, conn->writeBuffer().maxSize());
conn->writeBuffer().maxSize() returns the connection's buffer max size, which comes from the matched server's config at connection time — that's 10M (the server-level setting), not the location-level 30M. So the CGI result buffer cap is actually 10MB.
Write a Python script that outputs more than 10MB:
python#!/usr/bin/env python3

#### place at /home/fahd/fork/webserer_core/www/html/cgi-bin/big.py
print("Content-Type: text/plain\r")
print("\r")
print("A" * (11 * 1024 * 1024))  # 11MB — exceeds 10M cap
Then trigger it:
bashcurl http://127.0.0.1:8080/cgi-bin/big.py
The CGI child writes 11MB into the pipe. Your _handleCgiEvent drains the pipe in a loop calling job->result_buffer.append(buf, n). When accumulated bytes cross 10MB, Buffer::append throws BodyLimitException. Nothing catches it. Server dies.
To confirm it's the crash and not something else, run the server in a terminal and watch it exit with an unhandled exception message when you hit that endpoint.
The fix is one try-catch in _handleCgiEvent:
cppvoid EventLoop::_handleCgiEvent(int result_fd, uint32_t events)
{
    std::map<int, CgiJob*>::iterator it = _cgi_jobs.find(result_fd);
    if (it == _cgi_jobs.end())
        return;

    CgiJob* job = it->second;

    if (events & (EPOLLERR | EPOLLHUP))
    {
        // still try to drain
    }

    char buf[8192];
    try                          // ← add this
    {
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
    catch (const BodyLimitException&)   // ← and this
    {
        _failCgiJob(result_fd, 502);
    }
}
That turns a server crash into a clean 502 Bad Gateway.












