## Classes Created So Far

### 1. `Buffer` — `Buffer.hpp`
What it is: Dynamic byte buffer for raw I/O
Used by: Connection (owns two instances — read and write)

    read_buffer  ← raw bytes in  (from recv())
    write_buffer ← raw bytes out (to   send())

──────────────────────────────────────────────────────────────

### 2. `HttpRequest` — `HttpRequest.hpp`
What it is: Plain data bag for one parsed HTTP request
Used by: Parser fills it, Processing reads it, CGI reads it

    method / path / query_string / version
    headers / body
    parse_state / content_length / chunked

### 2b. `ParseState` — (inside HttpRequest.hpp)
What it is: Enum tracking parser progress

    PS_IDLE → PS_REQUEST_LINE → PS_HEADERS → PS_BODY → PS_COMPLETE
                                                      → PS_ERROR

──────────────────────────────────────────────────────────────

### 3. `ConnectionState` — `ConnectionState.hpp`
What it is: Enum driving the Connection state machine
Used by: Connection to decide what epoll watches

    CS_READING    → EPOLLIN
    CS_PROCESSING → nothing
    CS_WRITING    → EPOLLOUT
    CS_CLOSING    → nothing

──────────────────────────────────────────────────────────────

### 4. `Connection` — `Connection.hpp`
What it is: One live TCP client connection
Owns:
    int                 _fd
    const ServerConfig* _config        ← borrowed
    Buffer              _read_buffer   ← from Buffer.hpp
    Buffer              _write_buffer  ← from Buffer.hpp
    HttpRequest         _request       ← from HttpRequest.hpp
    ConnectionState     _state         ← from ConnectionState.hpp
    time_t              _last_active

──────────────────────────────────────────────────────────────

### 5. `ConnectionManager` — `ConnectionManager.hpp`
What it is: The ONLY class allowed to create/destroy Connections
Owns:
    std::map<int, Connection*>  _connections
    int                         _epoll_fd    ← borrowed

Key rules it enforces:
    CREATE  → new Connection → map insert → epoll ADD
    DESTROY → epoll DEL → delete → erase   (always in this order)

──────────────────────────────────────────────────────────────

### 6. `EventLoop` — `EventLoop.hpp`
What it is: The core epoll dispatch loop
Owns:
    int                         _epoll_fd
    ConnectionManager*          _manager
    std::vector<int>            _server_fds
    std::vector<ServerConfig*>  _server_configs

Dispatches:
    server fd  → _handleAccept()
    EPOLLERR   → _handleError()
    EPOLLIN    → _handleRead()
    EPOLLOUT   → _handleWrite()

──────────────────────────────────────────────────────────────

──────────────────────────────────────────────────────────────

### 7. `ResponseHandler` — `ResponseHandler.hpp`
What it is: Reads a complete HttpRequest + ServerConfig, builds the full HTTP response, writes raw bytes into write_buffer
Used by: EventLoop._handleRead() — replaces _stubBuildResponse / _stubSend400 / _stub413

    handle(req, cfg, wb)    → full decision tree → GET/HEAD/POST/DELETE
    sendError(code, cfg, wb) → pre-request errors (400/413/414/431/505), Connection: close

Decision tree inside handle():
    1. Path traversal guard        → 400
    2. matchLocation(req.path)     → Location*
    3. Method check                → 405
    4. Redirect check              → 301/302 + Location header
    5. Resolve fs_path = root + path
    6. Directory URI (ends '/'):
           try index file          → serveStaticFile
           autoindex on            → sendDirectoryListing (HTML table)
           else                    → 403
    7. stat(fs_path)               → 404 if missing; 301 if dir without slash
    8. CGI extension match         → _stubCgi() [Phase 4 seam]
    9. GET/HEAD                    → serveStaticFile (fstat + read loop)
       POST + upload_path          → handlePost (uniq filename, write body, 201)
       DELETE                      → handleDelete (unlink, 204)
       other                       → 405

Internal helpers:
    _loadErrorPage()   → config.error_pages[code] → disk read → builtinErrorBody fallback
    _getMimeType()     → extension → Content-Type (24 types)
    _httpDate()        → gmtime + strftime → RFC 7231 GMT format
    _sendErrorInternal() → like sendError but respects request keep-alive state
    _stubCgi()         → 501 stub, replaced in Phase 4 by CgiHandler::execute()

──────────────────────────────────────────────────────────────

### Dependency Chain

    Buffer.hpp
        └── HttpRequest.hpp
                └── ConnectionState.hpp
                        └── Connection.hpp
                                └── ConnectionManager.hpp
                                        └── EventLoop.hpp
                                                │
                                        HttpParser.hpp (Phase 2 ✓)
                                        ResponseHandler.hpp (Phase 3 ✓)
                                                └── ServerConfig.hpp
                                                └── CgiHandler.hpp (Phase 4)

──────────────────────────────────────────────────────────────

──────────────────────────────────────────────────────────────

### 8. `Logger` — `Logger.hpp`  (header-only)
What it is: Intercepts std::cerr via TeeStreambuf, mirrors every byte to both the terminal and a log file
Used by: main.cpp only — no other file needs to be touched

    Logger::instance().open("webserv.log")   ← call at startup
    Logger::instance().close()               ← call before return

How it works:
    TeeStreambuf  replaces std::cerr's rdbuf
    overflow()    writes each char to _orig (terminal) + _file (log)
    Timestamps    injected at start of every new line in the log file only
    Format        [2024-11-04 12:00:00] [EventLoop] starting
    Flush         after every '\n' → real-time tail -f works

Zero changes to any other translation unit — all existing std::cerr calls
in EventLoop, ConnectionManager, ResponseHandler, etc. are captured automatically.

──────────────────────────────────────────────────────────────

### What is still stubbed / not yet written

    ServerConfig  → stub in Headers/ServerConfig.hpp — teammate replaces with real parser
    CgiHandler    → _stubCgi() in ResponseHandler returns 501 — Phase 4 fills it in