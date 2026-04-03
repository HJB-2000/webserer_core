# Change Tracker

## Current Repository State (as of Phase 2 completion)

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

### Headers NOT YET CREATED (Phase 3+)
| Header | Phase | Role |
|--------|-------|------|
| `Headers/ResponseHandler.hpp` | 3 | Builds HTTP responses into write Buffer |
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

## Current Integration Seams in EventLoop.cpp

These are the three remaining stubs — Phase 3 replaces them:

```cpp
// _stubBuildResponse(conn)  →  _responder.handle(conn->request(), *conn->config(), conn->writeBuffer())
// _stubSend400(conn)        →  _responder.sendError(400, *conn->config(), conn->writeBuffer())
// _stub413(conn)            →  _responder.sendError(413, *conn->config(), conn->writeBuffer())
```

---

## Compile & Test

```bash
make re                                   # clean build
./webserv                                 # start server (port 8080)
curl -v http://localhost:8080/            # happy path → 200
printf "BADREQUEST\r\n\r\n" | nc localhost 8080   # error path → 400
```

---

## Next Step — Phase 3: ResponseHandler

Create `Headers/ResponseHandler.hpp` and `ResponseHandler.cpp`.

Interface:
```cpp
class ResponseHandler {
public:
    void handle(const HttpRequest& req, const ServerConfig& cfg, Buffer& out);
    void sendError(int code,            const ServerConfig& cfg, Buffer& out);
};
```

Decision tree inside `handle()`:
1. `matchLocation(req.path)` → get Location
2. Check method against `location->allowed_methods` → 405
3. Check `location->redirect_enabled` → 301/302
4. Resolve `fs_path = root + req.path`
5. Directory? → try index file → autoindex → 403
6. `stat(fs_path)` fails → 404
7. CGI extension matches → CgiHandler (Phase 4)
8. GET/HEAD → `serveStaticFile`; POST → `handlePost`; DELETE → `handleDelete`

Add `ResponseHandler _responder` as a member of EventLoop.
