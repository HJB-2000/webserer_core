# Change Tracker

## Current Repository State (as of Phase 1 integration)

---

## File Inventory

### Source files
| File | Status | Role |
|------|--------|------|
| `main.cpp` | Active | Entry point — creates server socket, starts EventLoop |
| `Connection.cpp` | Active | Connection methods (recv, send, state transitions) |
| `EventLoop.cpp` | Active | epoll dispatch loop — all event handling |
| `HttpParser.cpp` | Active | HTTP/1.x request parser — all four phases |
| `Makefile` | Active | Build: `make` / `make re` / `make clean` / `make fclean` |

### Headers — ALL IN USE
| Header | Used by | Role |
|--------|---------|------|
| `Headers/buffer.hpp` | `Connection.hpp`, `HttpParser.hpp` | Dynamic byte buffer; throws `BodyLimitException` on overflow |
| `Headers/HttpRequest.hpp` | `Connection.hpp`, `HttpParser.hpp` | Parsed request data bag + `ParseState` enum |
| `Headers/ConnectionState.hpp` | `Connection.hpp` | `CS_READING / CS_PROCESSING / CS_WRITING / CS_CLOSING` enum |
| `Headers/Connection.hpp` | `ConnectionManager.hpp`, `EventLoop.hpp` | One live TCP connection — owns both buffers and HttpRequest |
| `Headers/ConnectionManager.hpp` | `EventLoop.hpp` | Owns the connections map; enforces lifecycle + epoll ADD/MOD/DEL |
| `Headers/EventLoop.hpp` | `main.cpp` | epoll dispatch loop declarations |
| `Headers/HttpParser.hpp` | `EventLoop.hpp`, `HttpParser.cpp` | Stateless parser interface — `void feed(Buffer&, HttpRequest&)` |
| `Headers/ServerConfig.hpp` | `ConnectionManager.hpp`, `Connection.cpp`, `EventLoop.cpp`, `main.cpp` | Integration bridge — `#include "serverConfig.hpp"` + `typedef Server ServerConfig` |
| `Headers/tmpconf.hpp` | `main.cpp` | Hardcoded fallback values — stays until main.cpp wiring is complete |

### Phase 3 + Logger headers (NEW — in use)
| Header | Role |
|--------|------|
| `Headers/ResponseHandler.hpp` | Builds HTTP responses into write Buffer — Phase 3 ✓ |
| `Headers/Logger.hpp` | TeeStreambuf + Logger singleton — tees std::cerr to log file ✓ |

### Teammate config-parser sources (Phase 1 — NOW IN BUILD)
| File | Role |
|------|------|
| `conf/parsing.cpp` | Comment stripping, whitespace normalization, token splitting |
| `conf/LexerConfig.cpp` | Token classification → `Lexer` objects with type + value |
| `conf/eventsConfig.cpp` | `eventsConfig` class (worker_connections, event_model) |
| `conf/serverConfig.cpp` | `Server` class + `matchLocation()` + `matchServer()` |
| `conf/locationConfig.cpp` | `Location` class with full getter interface |
| `conf/httpConfig.cpp` | `httpConfig` class — http-block-level directives + parse helpers |
| `conf/parserConf.cpp` | Top-level parser driver + `report_parse_error()` + `parse_cl_mx_bd_sz()` |
| `conf/server_parser.cpp` | Server-block directive parsing + `check_for_allowed_methods()` |
| `conf/location_parser.cpp` | Location-block directive parsing |
| `conf/printer.cpp` | `ParserConf::printer_of_conf_parser()` — debug dump |

### Headers NOT YET CREATED (Phase 4)
| Header | Phase | Role |
|--------|-------|------|
| `Headers/CgiHandler.hpp` | 4 | Forks CGI processes, parses output |

---

## What Changed — Session by Session

### Session 5 — Phase 1 integration (conf/ merged into build)
**Deleted:**
- `src/ServerConfig.cpp` — stub removed; teammate's `conf/serverConfig.cpp` takes over

**Modified files:**
- `Headers/ServerConfig.hpp` — replaced stub class definitions with bridge: `#include "serverConfig.hpp"` + `typedef Server ServerConfig`
- `Headers/EventLoop.hpp` — `class ServerConfig;` → `class Server; typedef Server ServerConfig;`
- `Headers/Connection.hpp` — same forward-declaration fix
- `src/Connection.cpp` — `config->client_max_body_size` → `config->getMaxBody()`
- `src/ConnectionManager.cpp` — `cfg->timeout_seconds` → `cfg->get_timeout_seconds()`
- `src/ResponseHandler.cpp` — all 12 direct field accesses replaced with getter calls:
  - `cfg.root` → `cfg.getRoot()`, `cfg.getIndex_s()[0]`, `cfg.getErrorPageMap()`
  - `loc->allowed_methods` → `loc->getMethods()`
  - `loc->redirect_enabled/code/url` → `loc->getRedirectEnabled()` / `getReturnRedirection_code()` / `getReturnRedirection_path()`
  - `loc->upload_path` → `loc->getUploadStore()`
  - `loc->autoindex` → `loc->getAutoindex()`
  - `loc->cgi_extension` → `loc->getCGI_extension()`
  - `loc->root` → `loc->getRoot()`
- `Makefile` — added `-I conf`; added 10 `conf/*.cpp` sources; removed `src/ServerConfig.cpp`

**Result:** `make re` — 19 object files, zero warnings, zero errors.

### Session 1 — Core skeleton (Phase 0)
- Created full epoll non-blocking I/O skeleton
- `buffer.hpp`, `HttpRequest.hpp`, `ConnectionState.hpp`, `Connection.hpp`,
  `ConnectionManager.hpp`, `EventLoop.hpp`, `ServerConfig.hpp` (stub), `tmpconf.hpp`
- `Connection.cpp`, `EventLoop.cpp`, `main.cpp`
- All stubs in EventLoop: `_stubParse`, `_stubBuildResponse`, `_stubSend400`, `_stub413`

### Session 2 — HttpParser (Phase 2)
**New files:**
- `Headers/HttpParser.hpp` — stateless parser class with four private phase methods
- `HttpParser.cpp` — full implementation:
  - `_parseRequestLine()`: nginx-style state machine; 414 on URI > 8192; 505 on bad version
  - `_parseHeaders()`: incremental line-by-line; 431 on line > 8192 or > 100 headers; 400 on missing Host (HTTP/1.1)
  - `_parseBody()`: Content-Length path; resumes across recv() calls
  - `_parseChunked()`: three-state resumable loop; 400 on bad hex size
- `Makefile` — added `HttpParser.cpp` to SRCS

**Modified files:**
- `Headers/HttpRequest.hpp` — added `error_code`, `_chunk_size`, `_chunk_trailing`, `_chunk_done`; updated constructor and `reset()`
- `Headers/EventLoop.hpp` — replaced `class HttpParser;` forward decl with `#include "HttpParser.hpp"`; added `HttpParser _parser` member; removed `_stubParse` declaration
- `EventLoop.cpp` — replaced `_stubParse(conn)` with `_parser.feed(conn->readBuffer(), conn->request())`; added `error_code` logging; removed `_stubParse()` implementation; added `#include "Headers/HttpParser.hpp"`

**Deleted (repository cleanup):**
- `a.out`, `Connection.o`, `EventLoop.o` — stale build artifacts
- `server` — old binary
- `Headers/webserver.hpp` — superseded old class
- `Headers/tmp_base_data_structured.hpp` — design notes in a header
- `http_parser/` — reference directory (logic absorbed into HttpParser.cpp)

---

### Session 3 — ResponseHandler (Phase 3)
**New files:**
- `Headers/ResponseHandler.hpp` — full class declaration with all private helpers
- `src/ResponseHandler.cpp` — full implementation:
  - `handle()`: 9-step decision tree (traversal guard → location → method → redirect → fs_path → directory → stat → CGI seam → dispatch)
  - `_serveStaticFile()`: `open` + `fstat` + `read` loop; HEAD skips body
  - `_sendDirectoryListing()`: `opendir`/`readdir` loop; HTML table with file sizes and trailing-slash links
  - `_sendRedirect()`: status line + `Location:` header + short HTML body
  - `_handlePost()`: saves `req.body` to `upload_path/upload_<ts>_<pid>_<N>`; 201 Created + `Location:` header
  - `_handleDelete()`: `unlink()`; 204 No Content; 403 on EACCES/EPERM; 500 on other failure
  - `_loadErrorPage()`: reads custom page from `config.error_pages[code]`; falls back to `_builtinErrorBody()`
  - `_getMimeType()`: lowercase extension lookup in 24-entry `std::map`; defaults to `application/octet-stream`
  - `_httpDate()`: `gmtime` + `strftime` → RFC 7231 format
  - `_stubCgi()`: returns 501; Phase 4 seam — replace with `CgiHandler::execute()`
- `Makefile` — added `src/ResponseHandler.cpp` to SRCS

**Modified files:**
- `Headers/EventLoop.hpp` — removed stub declarations (`_stubBuildResponse`, `_stubSend400`, `_stub413`); replaced `class ResponseHandler;` forward decl with `#include "ResponseHandler.hpp"`; added `ResponseHandler _responder` member
- `src/EventLoop.cpp` — replaced all three stub calls with real `_responder.handle()` / `_responder.sendError()` calls; removed stub implementations; added `#include "Headers/ResponseHandler.hpp"`

**Deleted:**
- `response_handler/` — nginx reference directory (logic absorbed into ResponseHandler.cpp)

---

### Session 4 — Logger (log file handler)
**New files:**
- `Headers/Logger.hpp` — header-only; `TeeStreambuf` + `Logger` singleton
  - `TeeStreambuf::overflow()` writes each char to terminal AND log file; injects UTC timestamp at start of each log line
  - `TeeStreambuf::xsputn()` routes bulk writes through overflow() so timestamp injection is never bypassed
  - `Logger::open(path)` replaces `std::cerr.rdbuf()` with the tee; writes session-start banner
  - `Logger::close()` restores original rdbuf, writes session-end banner, flushes and closes file
  - Log format per line: `[YYYY-MM-DD HH:MM:SS] original message`
  - File opened in append mode — multiple server runs accumulate in one file
  - Flush after every `\n` — `tail -f webserv.log` works in real time

**Modified files:**
- `src/main.cpp` — added `#include "Headers/Logger.hpp"`; `Logger::instance().open("webserv.log")` at startup; `Logger::instance().close()` before `return 0`

**No other files changed** — all existing `std::cerr` calls in EventLoop, ConnectionManager, ResponseHandler, HttpParser, etc. are captured automatically without modification.

---

## Current Integration Seams

```cpp
// Phase 4 — ResponseHandler::_stubCgi():
//   Replace body with: CgiHandler cgi(req, cfg, *loc); cgi.execute(wb);

// Phase 5 — main.cpp (after teammate fixes error.md bugs):
//   Replace TMP_HOST/TMP_PORT stub with:
//     ParserConf parser;
//     // ... tokenize argv[1] → call parser
//     const vector<Server>& servers = parser.get_http().get_all_servers();
//     for each server → make_listener(s.getHost(), s.getPort()) → loop.addServerSocket(fd, &s)
```

---

## Compile & Test

```bash
make re                                          # clean build
./webserv                                        # start server (port 8080)
tail -f webserv.log                              # watch logs in real time
curl -v http://localhost:8080/                   # GET → 200 index or autoindex
curl -v http://localhost:8080/missing.html       # GET → 404
curl -X DELETE http://localhost:8080/file.txt    # DELETE → 204 or 404
curl -X POST --data-binary @f.txt http://localhost:8080/uploads/  # POST → 201
printf "BADREQUEST\r\n\r\n" | nc localhost 8080  # malformed → 400
```

---

## Next Steps

**Unblock (teammate):** Fix bugs listed in `error.md` — then wire main.cpp.

**Phase 4 (you):** Create `Headers/CgiHandler.hpp` + `src/CgiHandler.cpp`.
Replace `ResponseHandler::_stubCgi()` body with `CgiHandler cgi(req, cfg, *loc); cgi.execute(wb);`
