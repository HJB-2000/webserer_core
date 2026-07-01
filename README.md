# Webserv

*This project has been created as part of the 42 curriculum by jbahmida, fbenalla.*

---

## Description

Webserv is an HTTP server implemented in C++ 98, designed to handle HTTP requests and deliver web content. This project provides a complete understanding of how the Hypertext Transfer Protocol (HTTP) works—the foundation of data communication for the World Wide Web.

### What is HTTP?

The Hypertext Transfer Protocol (HTTP) is an application protocol for distributed, collaborative, hypermedia information systems. HTTP is the foundation of data communication for the World Wide Web, where hypertext documents include hyperlinks to other resources that users can easily access.

The primary function of a web server is to store, process, and deliver web pages to clients. Client-server communication occurs through HTTP. Pages delivered are most frequently HTML documents, which may include images, style sheets, and scripts in addition to the text content.

Although its primary function is to serve content, HTTP also enables clients to send data. This feature is used for submitting web forms, including the uploading of files.

### HTTP Protocol Versions

This server implements **HTTP/1.1**, which is the version referenced in the project subject. Here's a brief comparison with newer versions:

| Version | Key Features |
|---------|-------------|
| **HTTP/1.1** | Keep-alive connections, chunked transfer encoding, virtual hosting, persistent connections. This implementation uses a single event loop with epoll for efficient I/O multiplexing. |
| **HTTP/2** | Binary framing (instead of text), multiplexing (multiple requests/responses in parallel), header compression (HPACK), server push, stream prioritization. |
| **HTTP/3** | QUIC transport protocol instead of TCP, built-in encryption (TLS 1.3), connection establishment without handshake latency (0-RTT), improved handling of packet loss. |

---

## How Webserv Works

### The Request-Response Cycle

The workflow follows the classic client-server model:

```
[Client] --- HTTP Request ---> [Webserv Server]
[Client] <-- HTTP Response -- [Webserv Server]
```

1. A client (typically a web browser) initiates communication by requesting a specific resource using HTTP
2. The server receives the request, parses it, and processes it
3. The server responds with the content of that resource or an error message if unable to do so
4. The connection can be kept alive for subsequent requests (keep-alive)

---

## Technical Implementation

### 1. Core — Event Loop, Sockets, Connections

**Files:** `main.cpp`, `make_listener.cpp`, `EventLoop.*`, `EventLoop_helper*.cpp`, `Connection.*`, `ConnectionManager.*`, `ConnectionState.*`, `Buffer.*`, `EventRef.hpp`

#### Model
1 `epoll_fd` → all I/O. No blocking read/write outside an epoll-ready callback. No errno-based control flow post I/O.

#### Listener setup (`make_listener.cpp`)
- `socket()` → `AF_INET/SOCK_STREAM`
- `SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY`, `TCP_DEFER_ACCEPT`, `SO_RCVBUF/SNDBUF` tuning
- `bind()` → `listen(backlog = somaxconn)` → `setNonBlocking()`
- One fd per `host:port` in config → registered in EventLoop as `EV_SERVER`

#### EventLoop
- `EventRef{kind, fd}` tags every epoll entry: `EV_SERVER | EV_CLIENT | EV_CGI | EV_CGI_STDIN | EV_INVALID`
- `run()`: single loop → `epoll_wait` → `_dispatch` per event → reap stale refs → timeout sweep → reap zombie CGI pids
- `_dispatch` routes by `EventRef::kind`, never assumes fd type
- EPOLLET (edge-triggered) everywhere → `_handleRead` loops `recv()` until EAGAIN-equivalent (n<0) or peer close (n==0)
- Stale `EventRef*` deleted one epoll cycle later (`_stale_refs`), not during dispatch, to avoid use-after-free if multiple events for the same now-dead fd are in the same `epoll_wait` batch

#### Connection
- 1 object per accepted client fd. Owns: read `Buffer`, write `Buffer`, `HttpRequest`, `ConnectionState`, last-active timestamp, per-connection `ServerConfig*` (can be swapped on `Host:` header match for name-based routing → `override_host`)
- `ConnectionState`: `READING → PROCESSING → WRITING → READING` (keep-alive loop) or `→ CGI_RUNNING` or `→ CLOSING`
- `buildEpollEvent()` derives epoll flags from current state (EPOLLIN while reading, EPOLLOUT while writing) — state machine drives the poll mask, not the other way around
- `reset()`/`setReading()` recycle the connection object for keep-alive instead of realloc

#### ConnectionManager
- `std::map<fd, Connection*>` ownership; `_max_connections = max(somaxconn, 10000)`
- `accept()` → non-blocking → CLOEXEC → wrapped in `Connection` → tracked
- `getTimedOutFds()` skips connections mid-CGI (a slow CGI must not be killed by the client idle timeout)
- Centralizes fd lifecycle: nothing else calls `close()` on a client fd directly except via `closeConnection`

#### Buffer
- Growable `std::vector<char>` ring-ish buffer: `_head` offset + lazy compaction (`_compact()` only when consumed >= half) to avoid O(n) memmove on every `consume()`
- `append()` enforces `client_max_body_size` → throws `BodyLimitException` → caught at EventLoop level → 413
- Two size ceilings in play: per-connection `_cmbs` (from config / location) for client body, and the hardcoded `LimitRequestBody` (20MB) used by `append_result` for CGI output streaming — these are intentionally different limits for different data directions

#### Why epoll, edge-triggered, single instance
- Subject requires exactly **one** poll-equivalent call for **all** I/O (listen + client + CGI pipes). epoll chosen over poll/select for O(1) readiness vs O(n) fd scan.
- ET forces drain-loops (`while(recv()>0)`), which is why `_handleRead` is a loop, not a single call.

---

### 2. HttpParser — RFC-Based Request Parsing

**Files:** `HttpParser.cpp`, `HttpParser.hpp`

#### Pipeline (per connection, resumable across multiple `recv()` calls)
```
PSTATE_IDLE → REQUEST_LINE → HEADERS → BODY → COMPLETE
                                                → ERROR (any stage)
```
Each stage consumes only what it can fully parse from the current buffer and returns; partial data waits for the next `recv()`. No blocking, no assumption that a full request arrives in one read.

#### Request line — explicit state machine
Char-by-char FSM (`RLState`) instead of `sscanf`/regex: `RL_START → METHOD → SPACE → URI → "HTTP/" literal match → MAJOR.MINOR → CRLF`.

- Rejects control/invalid URI bytes (`is_uri_char`), malformed method tokens
- HTTP version: only `1.1` accepted; any other digit → `505`
- URI length capped at 8192 → `414`
- Method whitelist: `GET / HEAD / POST / DELETE` only (subject scope) → unknown method `501`
- Absolute-URI form (`http://host/path`) normalized to origin-form path
- Percent-decoding (`pctDecode`) — `%2F` deliberately **not** decoded to `/` to prevent path-traversal-via-encoding tricks
- `cleanPath()` collapses duplicate slashes, strips trailing slash (except root)
- Path existence is checked here (`stat()`) for early 403/404 — avoids walking into header parsing for a dead path

#### Headers
- Line-by-line via CRLF scan, folded into a `map<string,string>`; header name lower-cased per RFC 7230 case-insensitivity
- Singleton-header set (`host`, `content-length`, `transfer-encoding`, etc.) — duplicates of these are a `400`; other repeated headers are comma-joined per spec
- `Host` is mandatory for HTTP/1.1, validated against the host/port grammar (`validate_host`, a small FSM handling `IPv4`, bracketed IPv6 literals, and named hosts) — drives virtual-host config switch (`override_host`)
- Mutually-exclusive `Content-Length` + `Transfer-Encoding` → `400` (request smuggling defense)
- `Content-Length` parsed digit-by-digit with running overflow/max-body check, not `atoi` (avoids overflow and silently-truncated huge values)
- Header count capped at 100, header block capped at 8192 bytes → `431`
- No body framing (`POST` with neither length nor chunked) → `411`

#### Body
- Two independent decoders sharing the same `Buffer`-backed `req.body`:
  - **Fixed-length** (`_parseBody`): reads exactly up to `content_length`, tracks `written` across calls
  - **Chunked** (`_parseChunked`): own micro-FSM for `size[;ext]CRLF`, then `data CRLF`, then terminating `0CRLF CRLF` — hex size parsed with overflow guard (`MAX_ALLOWED_CHUNK`), chunk extensions scanned for control-byte injection
- Per-location body limit (`_applyLocationBodyLimit`) re-applied before every body chunk, since a `Host:`-triggered config swap can change the limit mid-request
- Exceeding the limit at any point → `413`, not deferred to the end

#### Design choices worth noting
- Hand-rolled FSMs everywhere instead of higher-level parsing helpers — required to parse incrementally over non-blocking partial reads without re-scanning from the start each time
- Parser only touches `Connection`'s buffer/request, never performs I/O itself — keeps it decoupled from the event loop and trivially testable in isolation

---

### 3. ResponseHandler — Status Codes & Method Implementations

**Files:** `ResponseHandler.cpp`, `ResponseHandler_helper.cpp`, `ResponseLoader.cpp`, `ResponseHandler.hpp`

Scope: full request → response dispatch for **GET, POST, DELETE** (HEAD explicitly rejected — `405` — not part of subject scope). CGI *invocation decision* lives here (`resolveCgiRequest`); CGI *execution* is the teammate's part.

#### Routing
- `handle()` is the single entry point: normalizes path (`normalizePath` — collapses `.././.`, rejects escapes above root), matches the most specific `Location` block, checks allowed methods for that location (or defaults to GET-only if no location), applies redirect if configured, then dispatches by method + path kind (file / directory / missing).

#### GET
- Static file: `open()` + `stat()` + size cap (`ALLOWEDSIZE` ~500MB, else `400`) + MIME lookup by extension (`_mime` table) + raw `read()` loop into the write `Buffer`
- Directory: tries each configured index file in order; if none exist and `autoindex` is on, generates an HTML listing (escaped names, `../` entry, file sizes) — otherwise `403`/`404`
- HEAD is intentionally unsupported per current scope and returns `405` early, before any path resolution

#### POST
- Only valid where `upload_store` is configured for the matched location; otherwise `405`
- Multipart parsing is hand-rolled (`parseMultipartFirstFile` / `extractBoundary`) — extracts boundary from `Content-Type`, locates first file part, extracts `filename=` + `Content-Type:` from the part headers, isolates the binary payload between header-end and the next boundary
- Falls back to writing the raw request body if not multipart
- Filename is sanitized (`sanitizeFilename` — strips path separators, rejects `./..`) before being joined to the upload directory — prevents path traversal via a crafted `filename=`
- Success → `201 Created` + `Location:` header pointing at the new resource

#### DELETE
- `std::remove()` on the resolved filesystem path; errno mapped to `404` (ENOENT) / `403` (EACCES/EPERM) / `500` (other) — `204 No Content` on success

#### Status codes & headers
- `_writeHeaders` centralizes status line + `Date` (RFC 1123 via `strftime`) + `Content-Type` + `Content-Length` + `Connection` (keep-alive vs close, derived from `HttpRequest::keepAlive()` which itself respects HTTP/1.0 vs 1.1 default-persistence rules)
- `_reasonPhrase` covers the full set of codes this server can emit: 2xx success, 3xx redirect, the 4xx set the parser/handler can raise (400/403/404/405/408/411/413/414/431), plus 500/501/502/504/505
- Error responses (`_sendErrorInternal` / `sendError`) load a configured custom error page per status code if one exists for that `ServerConfig`, falling back to a minimal built-in HTML page — satisfies the "default error pages if none provided" requirement
- `sendError` (no-request-context variant) is used for cases where no valid `HttpRequest` exists yet (e.g. malformed request line) and always forces `Connection: close`

#### Why these design choices
- All filesystem path resolution funnels through one routine (`_resolveFsPath`) shared between the existence check, the GET serve, and the DELETE target — one place to fix path/location/root logic instead of three
- Buffer-only output (`_appendStr` into the connection's write `Buffer`) — ResponseHandler never writes to the socket directly; that stays the EventLoop's job, keeping I/O ownership in one place

---

### 4. Configuration File

Inspired by the NGINX configuration model, Webserv uses a tree-like structure with contexts and directives.

#### Configuration Contexts

| Context | Description |
|---------|-------------|
| **http** | Contains global HTTP behavior and all server blocks |
| **server** | Virtual server handling requests, selected by `listen` and `server_name` |
| **location** | Route-specific rules defined within a server block |

#### Supported Directives
- `listen` — Define interface:port pairs for listening
- `server_name` — Domain names for virtual host selection
- `error_page` — Custom error pages for specific status codes
- `client_max_body_size` — Maximum allowed request body size
- `root` — Base directory for resolving file paths
- `index` — Default files to serve for directory requests
- `autoindex on/off` — Enable/disable directory listing
- `allowed_methods` — HTTP methods permitted for a location
- `return` — HTTP redirection (3xx responses)
- `upload_store` — Directory for client file uploads
- `cgi_extension` — File extensions that trigger CGI execution
- `timeout` — Client idle timeout for connection management

**Resource:** [Understanding NGINX Configuration Structure](https://www.digitalocean.com/community/tutorials/understanding-the-nginx-configuration-file-structure-and-configuration-contexts)

---

### 5. CGI (Common Gateway Interface)

CGI enables Webserv to execute external programs (Python, PHP, etc.) for dynamic content generation.

#### Process Lifecycle
- **Process-per-request model:** For each CGI request, the server:
  1. Forks a child process with `fork()`
  2. Extracts request metadata (method, headers, query string)
  3. Passes data via environment variables
  4. Executes the script with `execve()`
  5. Captures output and streams it back to the client

#### Environment Variables
CGI scripts receive request data through environment variables:
- `REQUEST_METHOD` — GET, POST, DELETE, etc.
- `QUERY_STRING` — URL-encoded parameters
- `HTTP_*` — Request headers
- `CONTENT_LENGTH` — Body size
- `CONTENT_TYPE` — Body MIME type

#### Chunked Request Handling
- Server un-chunks `Transfer-Encoding: chunked` bodies before passing to CGI
- CGI receives EOF as end-of-body marker
- Chunked responses from CGI are forwarded as-is (EOF marking end)

**Resource:** [CGI101 Book - CGI Basics](https://www.cgi101.com/book/ch3/text.html)

---

### 6. Cookies and Session Management (Bonus)

HTTP is a stateless protocol—each request is independent. Cookies solve this problem.

#### The Problem: HTTP is Stateless
Without state management:
- A user logs in successfully
- The server forgets the user on the next request
- Users would need to re-authenticate every time

#### The Solution: Cookies
Cookies maintain state across multiple HTTP requests:

```
[Client] ---- POST /login ----> [Server]
[Client] <--- Set-Cookie: id --- [Server]
        (browser stores cookie)
[Client] ---- GET /home + Cookie ---> [Server]
```

#### Cookie Attributes
- **Expires / Max-Age:** Defines cookie validity duration
- **HttpOnly:** Prevents JavaScript access (XSS protection)

**Resource:** [MDN - HTTP Cookies](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Cookies)

---

## Compliance Notes (Subject-Driven Constraints)

- ✓ Single `epoll_wait()` for listen + client + CGI fds combined — no per-component poll loops
- ✓ No `read()`/`recv()`/`write()`/`send()` outside an epoll-ready callback
- ✓ No errno inspection after I/O to steer logic (only used for diagnostic logging on definitive failures like `bind()`/`open()`, never after `recv`/`send`)
- ✓ `fork()` used only for CGI
- ✓ Non-blocking fds end-to-end (`setNonBlocking` on listener and every accepted client)
- ✓ Methods implemented: GET, POST, DELETE (HEAD explicitly out of scope / `405`)
- ✓ Per-request body size enforced before exceeding it, not after
- ✓ Default error pages generated when none configured

---

## Instructions

### Compilation

```bash
make
```

### Running the Server

```bash
./webserv [path_to_configuration_file]
```

- If a configuration file is provided, the server will use it
- If not, the server falls back to a default configuration path

### Example Configuration

```nginx
http {
        error_page 404 /errors/404.html;
        client_max_body_size 100M;
    server {
        listen 8080;
        server_name localhost;
        root ./www/html;
        index index.html;
        timeout 60;

        
        location / {
            allowed_methods GET POST DELETE;
            autoindex off;
        }
        
        location /uploads/ {
            client_max_body_size 1.9g;
            allowed_methods GET POST DELETE;
            upload_path ./www/html/uploads/;
            autoindex on;
        }
        
        location /cgi-bin/ {
            client_max_body_size 111m;
            allowed_methods GET POST;
            cgi_path /usr/bin/python3 /usr/bin/bash;
            cgi_ext .py .sh;
        }
    }
}
```

---

## Project Structure

```
webserv_test/
├── Headers/              # Header files
│   ├── Connection.hpp
│   ├── HttpParser.hpp
│   ├── ResponseHandler.hpp
│   └── ...
├── src/                  # Core implementation
│   ├── main.cpp
│   ├── Connection.cpp
│   ├── HttpParser.cpp
│   ├── ResponseHandler.cpp
│   └── ...
├── conf/                 # Configuration parsing
│   ├── parserConf.cpp
│   ├── httpConfig.cpp
│   ├── serverConfig.cpp
│   └── ...
├── cgi/                  # CGI implementation
│   ├── CgiHandler.cpp
│   └── ...
├── www/                  # Web root directory
├── Makefile
└── README.md
```

---

## Resources

- [RFC 7230 - HTTP/1.1 Message Syntax](https://datatracker.ietf.org/doc/html/rfc7230)
- [NGINX Configuration Guide](https://www.digitalocean.com/community/tutorials/understanding-the-nginx-configuration-file-structure-and-configuration-contexts)
- [CGI Programming 101](https://www.cgi101.com/book/ch3/text.html)
- [MDN Web Docs - HTTP](https://developer.mozilla.org/en-US/docs/Web/HTTP)
- [HTTP Cookie Guide](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/Cookies)

---

## AI Usage

AI tools were used throughout this project mainly as an information and research aid:

* **Knowledge gathering** — asking about HTTP/1.1 concepts, RFC 7230 details, and the expected behavior of specific mechanisms (epoll semantics, CGI environment variables, cookie/session handling, etc.) to understand *why* something works a certain way before implementing it.
* **Behavior clarification** — questioning how particular system calls or protocol features should behave in edge cases (e.g. chunked transfer encoding, EPOLLET semantics, CGI process lifecycle) and comparing that against NGINX's behavior.
* **Resource discovery** — getting pointed toward relevant documentation and references (RFCs, MDN, CGI specs) cited in the Resources section.
* **Documentation structuring** — help organizing this README into clear sections.
