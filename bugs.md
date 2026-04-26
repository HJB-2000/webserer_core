Comprehensive Bug Report - 42 Webserv Project
Executive Summary
After deep analysis of the webserv implementation against the 42 subject requirements, I've identified 23 bugs categorized by severity. The most critical issues involve out-of-bounds memory access, incorrect HTTP header validation, and protocol violations.
# CRITICAL SEVERITY (Crash/UB/Security)
## BUG-C1: Out-of-Bounds Array Access in return Directive Parser
File: conf/location_parser.cpp, Line 133
Issue: values[1] accessed without checking values.size() >= 2
Exploit: Config with return 301; (missing URL) causes OOB read → crash/garbage
Fix: Add if (values.size() < 2) report_parse_error(...) before accessing values[1]

## BUG-C2: Missing _index_Files Copy in Assignment Operator
File: conf/serverConfig.cpp, Lines 84-98
Issue: operator= copies all members BUT _index_Files is mysteriously present (actually it IS copied at line 93). However, the copy semantics need verification for shallow vs deep copy.
Note: On re-examination, _index_Files IS being copied. This may be a false positive in error.md.
## BUG-C3: is_valid_number("") Returns True (Empty String Validation Bypass)
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
## BUG-H1: Location Prefix Match Without Boundary Check
File: conf/serverConfig.cpp, Lines 29-48
Issue: path.find(loc_path) == 0 matches /api against /apidoc
Expected: Next char must be / or end-of-string
Fix: Add boundary check: if (path.length() > loc_path.length() && path[loc_path.length()] != '/') continue;
## BUG-H2: Configuration Order Dependency
File: conf/locationConfig.cpp + conf/server_parser.cpp
Issue: Location inherits Server state at parse time. If location / appears before root directive → validation fails
Impact: Semantically valid configs rejected due to directive ordering
Fix: Two-pass parsing: collect directives first, validate after full block parsed
## BUG-H3: Comment Parsing Breaks Valid Paths
File: conf/parsing.cpp, Lines 5-27
Issue: # anywhere treated as comment start, even in quoted strings or paths
Example: root /var/www#backup; truncates to root /var/www
Fix: Only treat # as comment when it starts a token (after whitespace)
## BUG-H4: Signed Char UB in isdigit()/isspace() Calls
File: Multiple files (parsing.cpp:36-39, parserConf.cpp:69-72)
Issue: Passing raw char to isdigit() is UB for non-ASCII bytes
Fix: Cast to unsigned char: isdigit(static_cast<unsigned char>(ch))
## BUG-H5: client_max_body_size Cap Inconsistent Across Levels
Files: parserConf.cpp:84-92, server_parser.cpp:208-214, location_parser.cpp:106-112
Issue: HTTP/server levels cap at 1 GiB, location level has NO cap
Subject Requirement: Maximum 1 GiB everywhere
Fix: Apply same cap in location_parser.cpp::parseDirective()
## BUG-H6: Host Header Port Not Stripped in Virtual Host Matching
File: conf/serverConfig.cpp, Lines 7-27
Issue: Host: example.com:8080 doesn't match server_name example.com
Impact: Virtual hosting broken when client includes explicit port
Fix: Strip :port suffix from Host header before comparison
## BUG-H7: timeout 0; Accepted Then Rejected
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
## BUG-M7: Error Page Paths With Semicolons
File: conf/serverConfig.cpp:258-259, conf/locationConfig.cpp multiple lines
Issue: Default error pages stored as "./errors/400.html;" (semicolon included)
Impact: open() fails, falls back to built-in HTML
Fix: Remove trailing ; from string literals
LOW SEVERITY
## BUG-L1: Duplicate Methods in allowed_methods Accepted
File: conf/location_parser.cpp, Lines 77-86
Issue: allowed_methods GET GET POST; silently accepted
## BUG-L2: Extra Values Ignored for Scalar Directives
File: server_parser.cpp:195-203 (root), location_parser.cpp:87-93 (autoindex)
Issue: root a b c; uses only first value, ignores extras without warning
## BUG-L3: atoi() Overflow Not Handled
File: Multiple locations (listen port, error codes, return codes)
Issue: Very large numbers pass is_valid_number() but overflow atoi()
Fix: Use strtol() with ERANGE check (already done in parse_cl_mx_bd_sz)
## BUG-L4: server_name Required But Subject Doesn't Mandate It
File: server_parser.cpp:304-305
Issue: Forces server_name even for single-server configs
## BUG-L5: Lexer Keywords Collide With Valid Location Paths
File: location_parser.cpp:10-16
Issue: location server { } rejected because server lexed as TYPE_CONTEXT
## BUG-L6: cgi_path/cgi_pass Treated as Distinct But Deduped Together
File: location_parser.cpp:28-33 + 94-105
Issue: Can specify both, second overwrites first without error
## BUG-L7: client_max_body_size 0 Means "Reject All" Not "Unlimited"
File: Buffer.cpp logic
Issue: Nginx semantics: 0 = unlimited. Here: 0 = reject everything
Fix: Document or change behavior
## BUG-L8: Dead Code - I_Want() and check_dup_path_locations() Declared But Undefined
Files: conf/parserConf.hpp:35, conf/serverConfig.hpp:45
Issue: Linker error if ever called
## BUG-L9: using namespace std; in Headers
Files: Multiple .hpp files in conf/
Issue: Pollutes global namespace, violates coding standards
