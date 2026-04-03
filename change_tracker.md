# Change Tracker

## Current Repository State (as of Phase 3 + Logger)

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
| `Headers/ServerConfig.hpp` | `ConnectionManager.hpp`, `Connection.cpp`, `EventLoop.cpp`, `main.cpp` | Config data container + `matchLocation()` — **STUB**, teammate replaces |
| `Headers/tmpconf.hpp` | `ServerConfig.hpp`, `main.cpp` | All hardcoded config values in one place — removed when Phase 1 lands |

### Phase 3 + Logger headers (NEW — in use)
| Header | Role |
|--------|------|
| `Headers/ResponseHandler.hpp` | Builds HTTP responses into write Buffer — Phase 3 ✓ |
| `Headers/Logger.hpp` | TeeStreambuf + Logger singleton — tees std::cerr to log file ✓ |

### Headers NOT YET CREATED (Phase 4+)
| Header | Phase | Role |
|--------|-------|------|
| `Headers/CgiHandler.hpp` | 4 | Forks CGI processes, parses output |
| `Headers/ConfigParser.hpp` | 1 (teammate) | Reads `.conf` file → `vector<ServerConfig>` |

---

## What Changed — Session by Session

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

All Phase 3 stubs are replaced.  Remaining open seam:

```cpp
// In ResponseHandler::_stubCgi() — Phase 4:
//   Replace body with: CgiHandler cgi(req, cfg, *loc); cgi.execute(wb);
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

## Next Step — Phase 4: CgiHandler

Create `Headers/CgiHandler.hpp` and `src/CgiHandler.cpp`.

Interface:
```cpp
class CgiHandler {
public:
    CgiHandler(const HttpRequest& req, const ServerConfig& cfg, const Location& loc);
    void execute(Buffer& write_buffer);
private:
    void _buildEnv();
    void _readOutput(int pipe_fd, Buffer& wb);
};
```

Then in `ResponseHandler::_stubCgi()` replace the 501 body with:
```cpp
CgiHandler cgi(req, cfg, *loc);
cgi.execute(wb);
```
