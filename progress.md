# Progress — webserv core

---

## Phase 0 — Networking Core (DONE)

- [x] Non-blocking listener setup (IPv4 + IPv6 when available)
- [x] All listener sockets registered in the epoll dispatch path
- [x] Edge-trigger epoll loop with clean accept / read / write / error dispatch
- [x] Multi-client accept drain loop (edge-trigger until EAGAIN)
- [x] Non-blocking read drain per client until EAGAIN
- [x] Write path with EPOLLOUT re-arm and Buffer drain loop
- [x] Connection state machine (CS_READING → CS_PROCESSING → CS_WRITING → CS_CLOSING)
- [x] Keep-alive reset without fd re-registration
- [x] Timeout sweep using per-connection config->timeout_seconds
- [x] Single fd close point in ~Connection() — no double-close possible
- [x] delete-before-erase ownership rule enforced in ConnectionManager
- [x] setNonBlocking() called before epoll_ctl ADD (fixed ordering bug)
- [x] 400 / 413 error responses queued into write path before close (fixed send bug)
- [x] BufferOverflowException caught in read loop → 413 queued and sent
- [x] SIGINT / SIGTERM clean shutdown via EventLoop::stop()
- [x] EventLoop split into EventLoop.hpp (declarations) + EventLoop.cpp (implementation)
- [x] Connection split into Connection.hpp (declarations) + Connection.cpp (implementation)
- [x] tmpconf.hpp — all hardcoded values in one place, grouped by replacement target
- [x] ServerConfig.hpp stub — same field names/types as Plan.md; drop teammate's file to replace
- [x] Raw request printed to stderr for core-phase testing

---

## Phase 1 — ServerConfig + ConfigParser (teammate)

- [ ] Real ServerConfig.hpp delivered (replaces Headers/ServerConfig.hpp stub)
- [ ] Config file parsed from argv[1] in main.cpp
- [ ] Multiple server blocks supported
- [ ] Virtual host matching via Host header (matchServer())
- [ ] Location matching via longest-prefix (matchLocation())
- [ ] main.cpp: replace TMP_HOST / TMP_PORT loop with per-config socket binding

---

## Phase 2 — HttpParser (owner: this repo)

- [ ] HttpParser::feed(Buffer&, HttpRequest&) implemented
- [ ] Request line parsed (method, path, query_string, version)
- [ ] Headers parsed (lower-cased keys, stored in HttpRequest::headers)
- [ ] Body parsed — Content-Length path
- [ ] Body parsed — chunked Transfer-Encoding path
- [ ] PS_ERROR set on all protocol violations (see Plan.md §3 error table)
- [ ] _stubParse() in EventLoop.cpp replaced with _parser.feed(...)

---

## Phase 3 — ResponseHandler (owner: this repo)

- [ ] Static file serving with MIME type detection
- [ ] Directory listing (autoindex)
- [ ] Custom error pages (falls back to hardcoded HTML)
- [ ] HTTP redirects (301 / 302)
- [ ] GET / HEAD / POST / DELETE dispatch
- [ ] File upload (POST to location->upload_path)
- [ ] _stubBuildResponse() replaced with _responder.handle(...)
- [ ] _stubSend400() / _stub413() replaced with _responder.sendError(...)

---

## Phase 4 — CgiHandler (owner: this repo)

- [ ] fork + execve flow with correct env variables
- [ ] stdin pipe (request body) + stdout pipe (CGI output)
- [ ] CGI output parsed into HTTP response
- [ ] Timeout: kill(pid, SIGKILL) + waitpid() + 504 response

---

## ServerConfig integration checklist

When `Headers/ServerConfig.hpp` arrives from teammate, go through this table
and replace every macro / stub usage with the real field access.

| # | File | Location | Replace | With |
|---|------|----------|---------|------|
| 1 | `main.cpp` | `make_listener(TMP_HOST, TMP_PORT)` | `TMP_HOST`, `TMP_PORT` | `config.host`, `config.port` (loop over configs vector) |
| 2 | `Connection.cpp` | ctor | `config->client_max_body_size` | already correct — no change needed |
| 3 | `Headers/ConnectionManager.hpp` | `closeTimedOut()` | `cfg->timeout_seconds` | already correct — no change needed |
| 4 | `EventLoop.cpp` | `_stubParse()` | stub version field | `conn->request().version` from real parser |
| 5 | `EventLoop.cpp` | `_stubBuildResponse()` | entire stub | `_responder.handle(conn->request(), *conn->config(), conn->writeBuffer())` |
| 6 | `EventLoop.cpp` | `_stubSend400()` | entire stub | `_responder.sendError(400, *conn->config(), conn->writeBuffer())` |
| 7 | `EventLoop.cpp` | `_stub413()` | entire stub | `_responder.sendError(413, *conn->config(), conn->writeBuffer())` |
| 8 | `HttpParser` | body size check | `TMP_CLIENT_MAX_BODY_SIZE` | `conn->config()->client_max_body_size` |
| 9 | `HttpParser` | URI length check | `TMP_MAX_URI_LENGTH` | keep as macro (HTTP spec limit) |
| 10 | `HttpParser` | header line check | `TMP_MAX_HEADER_LINE` | keep as macro (HTTP spec limit) |
| 11 | `HttpParser` | header count check | `TMP_MAX_HEADER_COUNT` | keep as macro (HTTP spec limit) |
| 12 | `ResponseHandler` | root path | `TMP_ROOT` | `conn->config()->root` (or `location->root` if set) |
| 13 | `ResponseHandler` | index file | `TMP_INDEX` | `conn->config()->index` (or `location->index` if set) |
| 14 | `ResponseHandler` | autoindex | `TMP_AUTOINDEX` | `location->autoindex` |
| 15 | `ResponseHandler` | error pages | `TMP_ERROR_PAGE_*` | `conn->config()->error_pages[code]` |
| 16 | `ResponseHandler` | method check | `TMP_ALLOW_*` | `location->allowed_methods` |
| 17 | `ResponseHandler` | redirect | `TMP_REDIRECT_*` | `location->redirect_enabled/code/url` |
| 18 | `ResponseHandler` | upload | `TMP_UPLOAD_PATH` | `location->upload_path` |
| 19 | `CgiHandler` | CGI trigger | `TMP_CGI_EXTENSION` | `location->cgi_extension` |
| 20 | `CgiHandler` | CGI binary | `TMP_CGI_PATH` | `location->cgi_path` |
| 21 | `CgiHandler` | CGI timeout | `TMP_CGI_TIMEOUT_SECONDS` | keep as macro (not a config field) |
| 22 | `Headers/ServerConfig.hpp` | entire file | stub | teammate's real file — delete stub |
