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

### Dependency Chain

    Buffer.hpp
        └── HttpRequest.hpp
                └── ConnectionState.hpp
                        └── Connection.hpp
                                └── ConnectionManager.hpp
                                        └── EventLoop.hpp

──────────────────────────────────────────────────────────────

### What is still stubbed / not yet written

    ServerConfig     → config pointer used everywhere but not defined
    HttpParser       → _stubParse()         in EventLoop
    ResponseHandler  → _stubBuildResponse() in EventLoop
    CgiHandler       → called from ResponseHandler