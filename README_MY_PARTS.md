# Webserv — My Parts

> Scope: Core (sockets, epoll, connections) + HttpParser + ResponseHandler.


## Definitions

A brain-storming for the important keywords related to the subject.

**Socket :** an abstraction connection end-point managed by the kernel and exposed to the application layer via a file descriptor under the logic of the VFS .

**Non-blocking I/O:** `read`/`write`/`recv`/`send` return immediately even with no data, instead of stalling the process.

**epoll:** Linux readiness-notification API. Tells you which fds are ready, instead of you checking each one.

**epoll_wait:** Blocking call that returns the list of ready fds. Subject requires exactly one of these for all I/O.

**EPOLLET (edge-triggered):** epoll fires once per state change, not once per "still ready." Forces a drain-loop (`recv()` until it returns nothing) or you miss data.

**EPOLLIN / EPOLLOUT:** Flags meaning "fd ready to read" / "fd ready to write."

**EPOLLRDHUP / EPOLLHUP / EPOLLERR:** Peer closed write side / fd fully closed / fd errored.

**Backlog (somaxconn):** Max pending connections the kernel queues before `accept()`.

**SO_REUSEADDR / SO_REUSEPORT:** Socket options letting a restarted server rebind a port without `TIME_WAIT`/conflict delay.

**TCP_NODELAY:** Disables Nagle's algorithm — sends packets immediately instead of batching.

**CLOEXEC (FD_CLOEXEC):** Closes a fd automatically across `exec()`. Stops fds leaking into a forked CGI child.

**State machine / FSM:** Logic expressed as a fixed set of states + transitions, no recursion or backtracking. Used for both connection lifecycle and HTTP parsing.

**ConnectionState:** This server's FSM for a client connection: `READING -> PROCESSING -> WRITING -> READING` (loop) or `CGI_RUNNING` / `CLOSING`.

**Keep-alive:** Reusing one TCP connection for multiple HTTP requests instead of opening a new one each time.

**Buffer compaction:** Shifting unread bytes to the start of a buffer to reclaim space, instead of growing forever.

**RFC 7230:** The HTTP/1.1 message-syntax spec this parser follows (request line, headers, framing).

**CRLF:** `\r\n` — the line terminator HTTP requires between header lines and at line ends.

**Request line:** First line of an HTTP request: `METHOD SP URI SP HTTP-VERSION CRLF`.

**Percent-encoding:** `%XX` escaping of bytes in a URI (e.g. `%20` = space). Decoding it is `pctDecode` here.

**Path traversal:** Using `../` or encoding tricks to escape the intended root directory.

**Singleton header:** A header that may appear at most once per request (e.g. `Host`, `Content-Length`); a duplicate is malformed, not merged.

**Content-Length:** Header declaring exact body size in bytes.

**Transfer-Encoding: chunked:** Body sent as a series of `size CRLF data CRLF` chunks, ending in a zero-size chunk, used when length isn't known upfront.

**Request smuggling:** Attack exploiting disagreement between `Content-Length` and `Transfer-Encoding` framing — why this server rejects having both.

**Host header / virtual host:** `Host:` identifies which site a request is for when one server answers multiple `server_name`s on the same port.

**MIME type:** String like `text/html` telling the client how to interpret response body bytes.

**multipart/form-data:** Body-encoding format for file uploads — payload split into named parts separated by a boundary string.

**Location block:** Config-defined rule set for a URL prefix (allowed methods, root, redirect, upload dir, autoindex, etc.).

**Autoindex:** Auto-generated HTML directory listing when no index file is found.

**Status line / Reason phrase:** First line of an HTTP response: `HTTP-VERSION SP STATUS-CODE SP REASON-PHRASE`.

**CGI (Common Gateway Interface):** Protocol for running an external program (PHP, Python, etc.) to generate a response. Referenced here only where this part decides *whether* to invoke it — execution itself is the teammate's part.

## 1. Core — Event Loop, Sockets, Connections

**Files:** `main.cpp`, `make_listener.cpp`, `EventLoop.*`, `EventLoop_helper*.cpp`, `Connection.*`, `ConnectionManager.*`, `ConnectionState.*`, `Buffer.*`, `EventRef.hpp`

### Model
1 `epoll_fd` -> all I/O. No blocking read/write outside an epoll-ready callback. No errno-based control flow post I/O.

### Listener setup (`make_listener.cpp`)
- socket() -> AF_INET/SOCK_STREAM
- SO_REUSEADDR, SO_REUSEPORT, TCP_NODELAY, TCP_DEFER_ACCEPT, SO_RCVBUF/SNDBUF tuning
- bind() -> listen(backlog = somaxconn) -> setNonBlocking()
- One fd per `host:port` in config -> registered in EventLoop as `EV_SERVER`

### EventLoop
- `EventRef{kind, fd}` tags every epoll entry: `EV_SERVER | EV_CLIENT | EV_CGI | EV_CGI_STDIN | EV_INVALID`
- `run()`: single loop -> `epoll_wait` -> `_dispatch` per event -> reap stale refs -> timeout sweep -> reap zombie CGI pids
- `_dispatch` routes by `EventRef::kind`, never assumes fd type
- EPOLLET (edge-triggered) everywhere -> `_handleRead` loops `recv()` until EAGAIN-equivalent (n<0) or peer close (n==0)
- Stale `EventRef*` deleted one epoll cycle later (`_stale_refs`), not during dispatch, to avoid use-after-free if multiple events for the same now-dead fd are in the same `epoll_wait` batch

### Connection
- 1 object per accepted client fd. Owns: read `Buffer`, write `Buffer`, `HttpRequest`, `ConnectionState`, last-active timestamp, per-connection `ServerConfig*` (can be swapped on `Host:` header match for name-based routing -> `override_host`)
- `ConnectionState`: `READING -> PROCESSING -> WRITING -> READING` (keep-alive loop) or `-> CGI_RUNNING` or `-> CLOSING`
- `buildEpollEvent()` derives epoll flags from current state (EPOLLIN while reading, EPOLLOUT while writing) — state machine drives the poll mask, not the other way around
- `reset()`/`setReading()` recycle the connection object for keep-alive instead of realloc

### ConnectionManager
- `std::map<fd, Connection*>` ownership; `_max_connections = max(somaxconn, 10000)`
- `accept()` -> non-blocking -> CLOEXEC -> wrapped in `Connection` -> tracked
- `getTimedOutFds()` skips connections mid-CGI (a slow CGI must not be killed by the client idle timeout)
- Centralizes fd lifecycle: nothing else calls `close()` on a client fd directly except via `closeConnection`

### Buffer
- Growable `std::vector<char>` ring-ish buffer: `_head` offset + lazy compaction (`_compact()` only when consumed >= half) to avoid O(n) memmove on every `consume()`
- `append()` enforces `client_max_body_size` -> throws `BodyLimitException` -> caught at EventLoop level -> 413
- Two size ceilings in play: per-connection `_cmbs` (from config / location) for client body, and the hardcoded `LimitRequestBody` (20MB) used by `append_result` for CGI output streaming — these are intentionally different limits for different data directions

### Why epoll, edge-triggered, single instance
- Subject requires exactly **one** poll-equivalent call for **all** I/O (listen + client + CGI pipes). epoll chosen over poll/select for O(1) readiness vs O(n) fd scan.
- ET forces drain-loops (`while(recv()>0)`), which is why `_handleRead` is a loop, not a single call.

---

## 2. HttpParser — RFC-Based Request Parsing

**Files:** `HttpParser.cpp`, `HttpParser.hpp`

### Pipeline (per connection, resumable across multiple `recv()` calls)
```
PSTATE_IDLE -> REQUEST_LINE -> HEADERS -> BODY -> COMPLETE
                                                -> ERROR (any stage)
```
Each stage consumes only what it can fully parse from the current buffer and returns; partial data waits for the next `recv()`. No blocking, no assumption that a full request arrives in one read.

### Request line — explicit state machine
Char-by-char FSM (`RLState`) instead of `sscanf`/regex: `RL_START -> METHOD -> SPACE -> URI -> "HTTP/" literal match -> MAJOR.MINOR -> CRLF`.
- Rejects control/invalid URI bytes (`is_uri_char`), malformed method tokens
- HTTP version: only `1.1` accepted; any other digit -> `505`
- URI length capped at 8192 -> `414`
- Method whitelist: `GET / HEAD / POST / DELETE` only (subject scope) -> unknown method `501`
- Absolute-URI form (`http://host/path`) normalized to origin-form path
- Percent-decoding (`pctDecode`) — `%2F` deliberately **not** decoded to `/` to prevent path-traversal-via-encoding tricks
- `cleanPath()` collapses duplicate slashes, strips trailing slash (except root)
- Path existence is checked here (`stat()`) for early 403/404 — avoids walking into header parsing for a dead path

### Headers
- Line-by-line via CRLF scan, folded into a `map<string,string>`; header name lower-cased per RFC 7230 case-insensitivity
- Singleton-header set (`host`, `content-length`, `transfer-encoding`, etc.) — duplicates of these are a `400`; other repeated headers are comma-joined per spec
- `Host` is mandatory for HTTP/1.1, validated against the host/port grammar (`validate_host`, a small FSM handling `IPv4`, bracketed IPv6 literals, and named hosts) — drives virtual-host config switch (`override_host`)
- Mutually-exclusive `Content-Length` + `Transfer-Encoding` -> `400` (request smuggling defense)
- `Content-Length` parsed digit-by-digit with running overflow/max-body check, not `atoi` (avoids overflow and silently-truncated huge values)
- Header count capped at 100, header block capped at 8192 bytes -> `431`
- No body framing (`POST` with neither length nor chunked) -> `411`

### Body
- Two independent decoders sharing the same `Buffer`-backed `req.body`:
  - **Fixed-length** (`_parseBody`): reads exactly up to `content_length`, tracks `written` across calls
  - **Chunked** (`_parseChunked`): own micro-FSM for `size[;ext]CRLF`, then `data CRLF`, then terminating `0CRLF CRLF` — hex size parsed with overflow guard (`MAX_ALLOWED_CHUNK`), chunk extensions scanned for control-byte injection
- Per-location body limit (`_applyLocationBodyLimit`) re-applied before every body chunk, since a `Host:`-triggered config swap can change the limit mid-request
- Exceeding the limit at any point -> `413`, not deferred to the end

### Design choices worth noting
- Hand-rolled FSMs everywhere instead of higher-level parsing helpers — required to parse incrementally over non-blocking partial reads without re-scanning from the start each time
- Parser only touches `Connection`'s buffer/request, never performs I/O itself — keeps it decoupled from the event loop and trivially testable in isolation

---

## 3. ResponseHandler — Status Codes & Method Implementations

**Files:** `ResponseHandler.cpp`, `ResponseHandler_helper.cpp`, `ResponseLoader.cpp`, `ResponseHandler.hpp`

Scope: full request -> response dispatch for **GET, POST, DELETE** (HEAD explicitly rejected — `405` — not part of subject scope). CGI *invocation decision* lives here (`resolveCgiRequest`); CGI *execution* is the teammate's part.

### Routing
- `handle()` is the single entry point: normalizes path (`normalizePath` — collapses `..`/`.`, rejects escapes above root), matches the most specific `Location` block, checks allowed methods for that location (or defaults to GET-only if no location), applies redirect if configured, then dispatches by method + path kind (file / directory / missing).

### GET
- Static file: `open()` + `stat()` + size cap (`ALLOWEDSIZE` ~500MB, else `400`) + MIME lookup by extension (`_mime` table) + raw `read()` loop into the write `Buffer`
- Directory: tries each configured index file in order; if none exist and `autoindex` is on, generates an HTML listing (escaped names, `../` entry, file sizes) — otherwise `403`/`404`
- HEAD is intentionally unsupported per current scope and returns `405` early, before any path resolution

### POST
- Only valid where `upload_store` is configured for the matched location; otherwise `405`
- Multipart parsing is hand-rolled (`parseMultipartFirstFile` / `extractBoundary`) — extracts boundary from `Content-Type`, locates first file part, extracts `filename=` + `Content-Type:` from the part headers, isolates the binary payload between header-end and the next boundary
- Falls back to writing the raw request body if not multipart
- Filename is sanitized (`sanitizeFilename` — strips path separators, rejects `.`/`..`) before being joined to the upload directory — prevents path traversal via a crafted `filename=`
- Success -> `201 Created` + `Location:` header pointing at the new resource

### DELETE
- `std::remove()` on the resolved filesystem path; errno mapped to `404` (ENOENT) / `403` (EACCES/EPERM) / `500` (other) — `204 No Content` on success

### Status codes & headers
- `_writeHeaders` centralizes status line + `Date` (RFC 1123 via `strftime`) + `Content-Type` + `Content-Length` + `Connection` (keep-alive vs close, derived from `HttpRequest::keepAlive()` which itself respects HTTP/1.0 vs 1.1 default-persistence rules)
- `_reasonPhrase` covers the full set of codes this server can emit: 2xx success, 3xx redirect, the 4xx set the parser/handler can raise (400/403/404/405/408/411/413/414/431), plus 500/501/502/504/505
- Error responses (`_sendErrorInternal` / `sendError`) load a configured custom error page per status code if one exists for that `ServerConfig`, falling back to a minimal built-in HTML page — satisfies the "default error pages if none provided" requirement
- `sendError` (no-request-context variant) is used for cases where no valid `HttpRequest` exists yet (e.g. malformed request line) and always forces `Connection: close`

### Why these design choices
- All filesystem path resolution funnels through one routine (`_resolveFsPath`) shared between the existence check, the GET serve, and the DELETE target — one place to fix path/location/root logic instead of three
- Buffer-only output (`_appendStr` into the connection's write `Buffer`) — ResponseHandler never writes to the socket directly; that stays the EventLoop's job, keeping I/O ownership in one place

---

## Compliance notes (subject-driven constraints these parts satisfy)

- Single `epoll_wait()` for listen + client + CGI fds combined — no per-component poll loops
- No `read()`/`recv()`/`write()`/`send()` outside an epoll-ready callback
- No `errno` inspection after I/O to steer logic (only used for diagnostic logging on definitive failures like `bind()`/`open()`, never after `recv`/`send`)
- `fork()` used only for CGI
- Non-blocking fds end-to-end (`setNonBlocking` on listener and every accepted client)
- Methods implemented: GET, POST, DELETE (HEAD explicitly out of scope / `405`)
- Per-request body size enforced before exceeding it, not after
- Default error pages generated when none configured
