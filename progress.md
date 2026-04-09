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
- [x] 400 / 413 error responses queued into write path before close
- [x] BodyLimitException caught in read loop → 413 queued and sent
- [x] SIGINT / SIGTERM clean shutdown via EventLoop::stop()
- [x] EventLoop split into EventLoop.hpp (declarations) + EventLoop.cpp (implementation)
- [x] Connection split into Connection.hpp (declarations) + Connection.cpp (implementation)
- [x] tmpconf.hpp — all hardcoded values in one place, grouped by replacement target
- [x] ServerConfig.hpp stub — same field names/types as Plan.md; drop teammate's file to replace

---

## Phase 1 — ServerConfig + ConfigParser (teammate — INTEGRATED)

- [x] conf/ directory compiled into the project (10 source files, -I conf flag)
- [x] Headers/ServerConfig.hpp replaced with bridge: includes serverConfig.hpp + typedef Server ServerConfig
- [x] src/ServerConfig.cpp stub deleted — teammate's conf/serverConfig.cpp supplies the implementation
- [x] Forward declarations updated in EventLoop.hpp and Connection.hpp
- [x] Location matching via longest-prefix (matchLocation()) — live from conf/serverConfig.cpp
- [x] Virtual host matching (matchServer()) — live from conf/serverConfig.cpp
- [x] Full config parser pipeline: file → lexer → tokens → ParserConf → httpConfig → Server/Location
- [ ] main.cpp: wire ConfigParser — replace TMP_HOST/TMP_PORT stub with loop over parsed servers
- [ ] Teammate bug fixes needed before wiring main.cpp (see error.md)

---

## Phase 2 — HttpParser (DONE)

- [x] HttpParser::feed(Buffer&, HttpRequest&) implemented
- [x] Request line parsed (method, path, query_string, version)
- [x] Headers parsed (lower-cased keys, stored in HttpRequest::headers)
- [x] Body parsed — Content-Length path
- [x] Body parsed — chunked Transfer-Encoding path
- [x] PS_ERROR set on all protocol violations (400 / 414 / 431 / 505)
- [x] _stubParse() in EventLoop.cpp replaced with _parser.feed(...)
- [x] HttpParser is a stateless member of EventLoop (one instance serves all connections)
- [x] error_code / _chunk_size / _chunk_trailing / _chunk_done added to HttpRequest

---

## Logging — Log File Handler (DONE)

- [x] `Headers/Logger.hpp` — header-only, no .cpp needed
- [x] `TeeStreambuf` intercepts `std::cerr` — mirrors to terminal + log file
- [x] UTC timestamp `[YYYY-MM-DD HH:MM:SS]` injected at start of every log line
- [x] File opened in append mode (`std::ios::app`) — restarts accumulate, not overwrite
- [x] Flush after every `\n` — `tail -f webserv.log` works in real time
- [x] Session start/end banners written to log for easy session separation
- [x] `Logger::instance().open("webserv.log")` called at top of `main()`
- [x] `Logger::instance().close()` called before `return 0`
- [x] Zero changes to EventLoop, ConnectionManager, ResponseHandler, HttpParser — all existing `std::cerr` calls captured automatically

---

## Phase 3 — ResponseHandler (DONE)

- [x] Static file serving with MIME type detection (24 types, case-insensitive)
- [x] Directory listing (autoindex) — HTML table with file sizes, parent dir link
- [x] Custom error pages (config.error_pages[code] → disk read → built-in HTML fallback)
- [x] HTTP redirects (301/302) — Location header + HTML body
- [x] Directory-without-slash → 301 to path + '/'
- [x] Path traversal guard (/../ → 400)
- [x] GET / HEAD / POST / DELETE dispatch
- [x] HEAD request → headers only, no body
- [x] File upload (POST to location->upload_path) → unique filename → 201 Created
- [x] DELETE → unlink() → 204 No Content; 403 on permission error
- [x] Connection header respects request.keepAlive() in all responses
- [x] HTTP Date header in RFC 7231 GMT format
- [x] _stubBuildResponse() replaced with _responder.handle(...)
- [x] _stubSend400() / _stub413() replaced with _responder.sendError(...)
- [x] CGI seam in _stubCgi() → returns 501 until Phase 4 lands
- [x] Compiles clean: -std=c++98 -Wall -Wextra -Werror

---

## Phase 4 — CgiHandler (next)

- [ ] CgiHandler class created (Headers/CgiHandler.hpp + src/CgiHandler.cpp)
- [ ] fork + execve flow with correct env variables (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, CONTENT_LENGTH, PATH_INFO, SCRIPT_FILENAME, SERVER_*, HTTP_* …)
- [ ] stdin pipe (request body) + stdout pipe (CGI output)
- [ ] CGI output parsed into HTTP response (Status header → code, headers + blank line + body)
- [ ] Timeout: kill(pid, SIGKILL) + waitpid() + 504 response
- [ ] ResponseHandler::_stubCgi() replaced with CgiHandler::execute() call

---

## Phase 5 — Wiring (main.cpp)

- [ ] Parse config file from argv[1]
- [ ] Create one server socket per ServerConfig block
- [ ] Register each socket with EventLoop::addServerSocket()
- [ ] Virtual host dispatch by Host header after connect

---

## ServerConfig integration checklist

| # | File | Status | Notes |
|---|------|--------|-------|
| 1 | `main.cpp` | pending | Replace `TMP_HOST`/`TMP_PORT` with loop over parsed servers |
| 2 | `Connection.cpp` | ✓ done | `config->getMaxBody()` via getter |
| 3 | `ConnectionManager.cpp` | ✓ done | `cfg->get_timeout_seconds()` via getter |
| 4 | `EventLoop.cpp` | ✓ done | `_responder.handle(...)` wired in |
| 5 | `EventLoop.cpp` | ✓ done | `_responder.sendError(error_code, ...)` wired in |
| 6 | `EventLoop.cpp` | ✓ done | `_responder.sendError(413, ...)` wired in |
| 7 | `HttpParser` | pending | Replace `TMP_CLIENT_MAX_BODY_SIZE` with `conn->config()->getMaxBody()` |
| 8 | `HttpParser` | keep macro | `TMP_MAX_URI_LENGTH` — HTTP spec limit |
| 9 | `HttpParser` | keep macro | `TMP_MAX_HEADER_LINE` — HTTP spec limit |
| 10 | `HttpParser` | keep macro | `TMP_MAX_HEADER_COUNT` — HTTP spec limit |
| 11 | `ResponseHandler` | ✓ done | all ServerConfig / Location access via getters |
| 12 | `CgiHandler` | pending (Phase 4) | `loc->getCGI_extension()` / `loc->getCGI_path()` |
| 13 | `CgiHandler` | keep macro | `TMP_CGI_TIMEOUT_SECONDS` — not a per-location field |
| 14 | `Headers/ServerConfig.hpp` | ✓ done | bridge header — includes teammate's real file + typedef |
