# HTTP Server Implementation Guide
## From Skeleton to Full Server — Step by Step

---

# Table of Contents

1. Overview and Philosophy
2. Phase 1 — ServerConfig
3. Phase 2 — HttpParser
4. Phase 3 — ResponseHandler
5. Phase 4 — CgiHandler
6. Phase 5 — Wiring Everything Together in main()
7. Phase 6 — Testing and Hardening

---

# 1. Overview and Philosophy

## What you have right now

You have a complete, working skeleton. Every data structure is defined,
every ownership rule is enforced, and the epoll loop dispatches events
correctly. The stubs in EventLoop are the exact seams where your real
logic plugs in.

    _stubParse()          → seam for HttpParser
    _stubBuildResponse()  → seam for ResponseHandler
    _stubSend400()        → seam for ResponseHandler::sendError()

## The golden rule going forward

    Never touch the skeleton files again unless you find a bug.
    All new logic lives in new files.
    The skeleton files are infrastructure — treat them like the kernel.

## The order matters

Each phase depends on the previous one.
Do NOT skip ahead. A broken ServerConfig will corrupt everything
that reads from it.

    ServerConfig    → everything reads config
    HttpParser      → fills HttpRequest, reads from Buffer
    ResponseHandler → reads HttpRequest, reads ServerConfig, writes Buffer
    CgiHandler      → reads HttpRequest, writes Buffer, forks processes
    main()          → wires everything into EventLoop

---

# 2. Phase 1 — ServerConfig

## Why this comes first

Every single class in the skeleton has a pointer to ServerConfig.
Right now it is a forward declaration — a ghost.
Nothing can be tested until this ghost becomes real.

## What ServerConfig must hold

ServerConfig is a plain data container. It does not DO anything.
It only DESCRIBES what the server is configured to do.
Think of it as a parsed nginx.conf in C++ form.

### Top-level server block fields

    std::string              host
        The IP address or hostname to bind to.
        Example: "0.0.0.0" or "127.0.0.1"

    int                      port
        The port to listen on.
        Example: 8080

    std::vector<std::string> server_names
        Virtual host names for this server block.
        Example: ["example.com", "www.example.com"]
        Used to match the Host header from the request.

    std::string              root
        The filesystem root for serving files.
        Example: "/var/www/html"
        All file paths are resolved relative to this.

    std::string              index
        Default file to serve when a directory is requested.
        Example: "index.html"

    size_t                   client_max_body_size
        Hard ceiling for request body size in bytes.
        This is the value passed to Buffer constructor.
        Example: 1048576 (1MB)
        If exceeded Buffer throws BufferOverflowException → 413

    int                      timeout_seconds
        How long a connection can be idle before being closed.
        EventLoop::closeTimedOut() uses this value.
        Example: 60

    std::map<int, std::string> error_pages
        Custom error page paths keyed by HTTP status code.
        Example: { 404 → "/errors/404.html", 500 → "/errors/500.html" }
        ResponseHandler checks this before building any error response.

### Location blocks

Each server can have multiple location blocks. A location block
overrides server-level settings for a specific URL prefix.

Create a nested struct or class called Location:

    struct Location {
        std::string              path
            The URL prefix this location matches.
            Example: "/cgi-bin/" or "/uploads/" or "/"

        std::string              root
            Override the server root for this path.
            Empty string means inherit from ServerConfig.

        std::string              index
            Override the default index file.
            Empty string means inherit from ServerConfig.

        std::vector<std::string> allowed_methods
            Which HTTP methods are permitted here.
            Example: ["GET", "POST"]
            If a request uses a method not in this list → 405

        bool                     autoindex
            If true and no index file found, serve a directory listing.
            If false → 403

        std::string              cgi_extension
            File extension that triggers CGI execution.
            Example: ".py" or ".php"
            Empty string means no CGI for this location.

        std::string              cgi_path
            Path to the CGI interpreter binary.
            Example: "/usr/bin/python3"

        std::string              upload_path
            Where uploaded files are stored.
            Only relevant for POST requests with file uploads.
            Example: "/var/www/uploads/"

        bool                     redirect_enabled
            If true, this location sends a redirect response.

        int                      redirect_code
            The HTTP redirect code. Example: 301 or 302

        std::string              redirect_url
            The URL to redirect to.
            Example: "https://www.newsite.com/"
    }

    std::vector<Location>    locations
        All location blocks for this server, in order of declaration.
        Matching is done by longest prefix — the most specific
        location wins.

## How to implement location matching

Write a method on ServerConfig:

    const Location* matchLocation(const std::string& path) const

    Algorithm:
        best_match = NULL
        best_length = 0
        for each location in locations:
            if path starts with location.path:
                if location.path.length() > best_length:
                    best_match = &location
                    best_length = location.path.length()
        return best_match

    This gives you longest-prefix matching, which is the nginx rule.

## How to implement virtual host matching

Write a function or method:

    const ServerConfig* matchServer(
        const std::vector<ServerConfig>& servers,
        const std::string& host_header,
        int port)

    Algorithm:
        for each server in servers:
            if server.port == port:
                for each name in server.server_names:
                    if name == host_header:
                        return &server
        // fallback: first server on that port
        for each server in servers:
            if server.port == port:
                return &server
        return NULL

## Config file parsing

You need to write a parser for your config file format.
Keep it simple — nginx-style blocks with braces.

    server {
        host        0.0.0.0
        port        8080
        server_name example.com www.example.com
        root        /var/www/html
        index       index.html
        client_max_body_size 1048576
        timeout     60

        error_page 404 /errors/404.html
        error_page 500 /errors/500.html

        location / {
            allowed_methods GET POST
            autoindex       off
            index           index.html
        }

        location /cgi-bin/ {
            allowed_methods GET POST
            cgi_extension   .py
            cgi_path        /usr/bin/python3
        }

        location /uploads/ {
            allowed_methods POST
            upload_path     /var/www/uploads/
        }
    }

Write a ConfigParser class that:
    1. Reads the file line by line
    2. Strips comments (everything after #)
    3. Identifies block openings (contains {)
    4. Identifies block closings (contains })
    5. Parses key-value pairs inside blocks
    6. Builds a std::vector<ServerConfig> and returns it

## What to test after Phase 1

    - Parse a config file with two server blocks
    - Parse a server block with three location blocks
    - Call matchLocation("/cgi-bin/script.py") → correct location
    - Call matchLocation("/") → fallback location
    - Call matchServer() with a Host header → correct ServerConfig
    - Verify client_max_body_size is passed correctly to Buffer

---

# 3. Phase 2 — HttpParser

## What the parser does

The parser reads raw bytes from read_buffer and fills the HttpRequest
object. It is a state machine that mirrors ParseState exactly.

    PS_IDLE         → waiting for first byte
    PS_REQUEST_LINE → reading METHOD SP path SP version CRLF
    PS_HEADERS      → reading key: value CRLF pairs, ends with CRLF CRLF
    PS_BODY         → reading body bytes up to content_length
    PS_COMPLETE     → request is fully parsed, ready for processing
    PS_ERROR        → protocol violation detected → 400

## The parser interface

Create a class HttpParser with one key method:

    void feed(Buffer& read_buffer, HttpRequest& request)

    This method is called by EventLoop::_handleRead() after every
    successful recv(). It replaces _stubParse().

    It reads from read_buffer, advances the parse state in request,
    and consumes bytes from read_buffer as they are processed.

    It must be resumable — if the full request has not arrived yet
    the method returns and waits for the next recv() call.

## Implementing the request line parser

The first line of every HTTP request looks like:

    GET /path?query HTTP/1.1\r\n

Steps:
    1. Search read_buffer for the first \r\n
    2. If not found yet — return, wait for more data
    3. Extract the line up to \r\n
    4. consume() those bytes + 2 (for \r\n) from read_buffer
    5. Split by spaces — must get exactly 3 tokens
    6. If not exactly 3 tokens → PS_ERROR
    7. Token 0 → request.method   (validate: GET POST DELETE etc)
    8. Token 1 → full URI         (split on ? to get path + query_string)
    9. Token 2 → request.version  (validate: HTTP/1.0 or HTTP/1.1 only)
    10. Transition to PS_HEADERS

## Implementing the header parser

After the request line, headers follow this format:

    Host: example.com\r\n
    Content-Type: text/html\r\n
    Content-Length: 42\r\n
    \r\n          ← empty line marks end of headers

Steps:
    1. Loop: search read_buffer for \r\n
    2. If line is empty (just \r\n) → headers are done
    3. Otherwise: split on first : to get key and value
    4. Strip leading/trailing whitespace from both key and value
    5. Lowercase the key (use std::transform with tolower)
    6. Insert into request.headers map
    7. consume() the line from read_buffer
    8. After empty line: check for Content-Length and Transfer-Encoding
       - if "content-length" header exists:
           request.content_length = parse as size_t
       - if "transfer-encoding" == "chunked":
           request.chunked = true
    9. If content_length == 0 and not chunked → PS_COMPLETE
    10. Otherwise → PS_BODY

## Implementing the body parser

### Fixed-length body (Content-Length):

    while read_buffer.size() > 0 and request.body.size() < content_length:
        bytes_needed = content_length - request.body.size()
        bytes_available = min(bytes_needed, read_buffer.size())
        append read_buffer.data()[0..bytes_available] to request.body
        read_buffer.consume(bytes_available)

    if request.body.size() == content_length:
        transition to PS_COMPLETE

### Chunked body (Transfer-Encoding: chunked):

Each chunk looks like:

    <hex-size>\r\n
    <data bytes>\r\n
    0\r\n
    \r\n

Steps per chunk:
    1. Read until \r\n to get the hex size line
    2. Parse hex string to integer → chunk_size
    3. If chunk_size == 0 → this is the terminal chunk → PS_COMPLETE
    4. Wait until read_buffer has chunk_size + 2 bytes available
    5. Append chunk_size bytes to request.body
    6. consume() chunk_size + 2 (for trailing \r\n) from read_buffer
    7. Repeat from step 1

## Error conditions the parser must handle

    - Method not in allowed set           → PS_ERROR (respond 405)
    - URI longer than 8192 bytes          → PS_ERROR (respond 414)
    - Header line longer than 8192 bytes  → PS_ERROR (respond 431)
    - More than 100 headers               → PS_ERROR (respond 431)
    - Version not HTTP/1.0 or HTTP/1.1    → PS_ERROR (respond 505)
    - Content-Length exceeds cmbs         → PS_ERROR (respond 413)
    - Malformed chunk size                → PS_ERROR (respond 400)
    - Missing Host header (HTTP/1.1)      → PS_ERROR (respond 400)

## Connecting HttpParser to EventLoop

In EventLoop::_handleRead() replace _stubParse() with:

    _parser.feed(conn->readBuffer(), conn->request());

HttpParser should be a member of EventLoop or instantiated per
Connection (simpler: one stateless parser instance in EventLoop,
since all state lives in HttpRequest and Buffer anyway).

## What to test after Phase 2

    - Feed a complete GET request in one chunk → PS_COMPLETE
    - Feed a POST request split across 3 recv() calls → PS_COMPLETE
    - Feed a chunked POST request → body correctly reassembled
    - Feed a malformed request line → PS_ERROR
    - Feed a request with Content-Length > cmbs → PS_ERROR → 413
    - Feed a request with no Host header → PS_ERROR → 400
    - Feed two pipelined requests back to back in one buffer →
      first completes, second starts fresh

---

# 4. Phase 3 — ResponseHandler

## What ResponseHandler does

ResponseHandler reads a complete HttpRequest and a ServerConfig,
decides what the response should be, builds it, and writes the raw
bytes into write_buffer. That is its entire job.

ResponseHandler replaces _stubBuildResponse() and _stubSend400()
in EventLoop.

## The ResponseHandler interface

    class ResponseHandler {
    public:
        void handle(
            const HttpRequest&  request,
            const ServerConfig& config,
            Buffer&             write_buffer
        );

        void sendError(
            int                 status_code,
            const ServerConfig& config,
            Buffer&             write_buffer
        );
    }

## The decision tree inside handle()

    1. Find the matching Location from config.matchLocation(request.path)
    2. Check if the method is in location.allowed_methods
       NO  → sendError(405, ...)
    3. Check if location has a redirect
       YES → sendRedirect(location.redirect_code, location.redirect_url)
    4. Resolve the filesystem path:
       fs_path = (location.root or config.root) + request.path
    5. Check if path ends with / (directory request)
       YES →
           try index file: fs_path + location.index
           if exists → serve that file
           else if location.autoindex → sendDirectoryListing(fs_path)
           else → sendError(403, ...)
    6. Check if file exists (use stat())
       NO  → sendError(404, ...)
    7. Check if the CGI extension matches
       YES → hand off to CgiHandler
    8. Check request.method
       GET  / HEAD → serveStaticFile(fs_path)
       POST        → handlePost(request, location, write_buffer)
       DELETE      → handleDelete(fs_path, write_buffer)
       other       → sendError(405, ...)

## Building an HTTP response

Every response has this structure:

    HTTP/1.1 <status_code> <reason_phrase>\r\n
    <Header-Name>: <value>\r\n
    <Header-Name>: <value>\r\n
    \r\n
    <body bytes>

Write a helper that builds this into a std::string then appends
it to write_buffer. Break it into two parts:
    - headers string  (everything up to and including the blank line)
    - body bytes      (the actual content)

This separation matters because HEAD requests send headers only.

## Implementing serveStaticFile

    1. Open the file with open() / fopen()
    2. Read its size with stat() → st_size
    3. Build response headers:
       - Status: 200 OK
       - Content-Type: detect from file extension (see MIME table below)
       - Content-Length: st_size
       - Connection: keep-alive or close (from request.keepAlive())
       - Date: current time in HTTP date format
    4. Append headers to write_buffer
    5. Read file contents and append to write_buffer
    6. Close the file

## MIME type table (minimum required)

    .html  → text/html
    .css   → text/css
    .js    → application/javascript
    .json  → application/json
    .png   → image/png
    .jpg   → image/jpeg
    .gif   → image/gif
    .ico   → image/x-icon
    .txt   → text/plain
    .pdf   → application/pdf
    other  → application/octet-stream

Store this as a std::map<std::string, std::string> initialized
once in the ResponseHandler constructor.

## Implementing sendError

    1. Check config.error_pages for this status code
       Found → try to read that file from disk
    2. If not found or file missing → use a hardcoded HTML fallback:
       "<html><body><h1>404 Not Found</h1></body></html>"
    3. Build response with correct status line and Content-Length
    4. Append to write_buffer

## Implementing sendRedirect

    1. Build a response with status code (301 or 302)
    2. Add Location: <redirect_url> header
    3. Body can be empty or a short HTML message
    4. Append to write_buffer

## Implementing handlePost

POST handling depends on the location config:

    If location.upload_path is set:
        Save request.body to a file in upload_path
        Generate a unique filename (timestamp + random suffix)
        Respond 201 Created with Location header pointing to the file

    If location.cgi_extension matches:
        Hand off to CgiHandler (Phase 4)

    Otherwise:
        Respond 405 Method Not Allowed

## Implementing handleDelete

    1. Resolve fs_path from request.path
    2. Check the file exists → stat()
    3. No  → sendError(404)
    4. Yes → unlink(fs_path.c_str())
    5. Success → respond 204 No Content
    6. Failure → sendError(500)

## Implementing directory listing (autoindex)

    1. Open the directory with opendir()
    2. Read entries with readdir() in a loop
    3. Skip . and ..
    4. Build an HTML page with a table or list of links
    5. Each entry links to its path relative to the request URL
    6. Directories should have a trailing / in the link
    7. Respond 200 OK with Content-Type: text/html

## HTTP date format helper

The Date header requires this format:
    Mon, 04 Nov 2024 12:00:00 GMT

Use strftime() with format string:
    "%a, %d %b %Y %H:%M:%S GMT"

Always use gmtime() not localtime() — HTTP dates are always UTC.

## Connecting ResponseHandler to EventLoop

In EventLoop::_handleRead() replace _stubBuildResponse() with:

    _responder.handle(
        conn->request(),
        *conn->config(),
        conn->writeBuffer()
    );

Replace _stubSend400() with:

    _responder.sendError(400, *conn->config(), conn->writeBuffer());

## What to test after Phase 3

    - GET /index.html → 200 with correct Content-Type and body
    - GET /missing.html → 404 with custom error page if configured
    - GET /dir/ with autoindex on → HTML directory listing
    - GET /dir/ with autoindex off, no index → 403
    - POST to a non-POST location → 405
    - DELETE an existing file → 204, file is gone
    - DELETE a missing file → 404
    - Redirect location → 301 with Location header
    - HEAD request → headers only, no body

---

# 5. Phase 4 — CgiHandler

## What CGI is

CGI (Common Gateway Interface) is the mechanism for running
external programs that generate dynamic responses. When a request
matches a CGI location, instead of serving a file you:

    1. Fork a child process
    2. Set up environment variables describing the request
    3. Connect the child's stdin  to the request body
    4. Connect the child's stdout to a pipe you read from
    5. Execute the CGI program
    6. Read the program's output
    7. Parse the output as an HTTP response
    8. Write it into write_buffer

## The CgiHandler interface

    class CgiHandler {
    public:
        CgiHandler(
            const HttpRequest&  request,
            const ServerConfig& config,
            const Location&     location
        );

        void execute(Buffer& write_buffer);

    private:
        void        _buildEnv();
        std::string _resolvePath();
        void        _readOutput(int pipe_fd, Buffer& write_buffer);
    }

## The environment variables you MUST set

CGI programs discover everything about the request through
environment variables. The minimum required set:

    REQUEST_METHOD      → request.method          e.g. "GET"
    QUERY_STRING        → request.query_string     e.g. "id=42"
    CONTENT_TYPE        → request.header("content-type")
    CONTENT_LENGTH      → request.body.size() as string
    PATH_INFO           → request.path
    PATH_TRANSLATED     → resolved filesystem path
    SCRIPT_FILENAME     → full path to the CGI script
    SCRIPT_NAME         → script portion of the path
    SERVER_NAME         → config.host
    SERVER_PORT         → config.port as string
    SERVER_PROTOCOL     → request.version          e.g. "HTTP/1.1"
    SERVER_SOFTWARE     → "webserv/1.0"
    GATEWAY_INTERFACE   → "CGI/1.1"
    HTTP_HOST           → request.header("host")
    HTTP_USER_AGENT     → request.header("user-agent")
    HTTP_ACCEPT         → request.header("accept")
    HTTP_COOKIE         → request.header("cookie")

    For every other header, prefix with HTTP_ and uppercase:
        x-custom-header → HTTP_X_CUSTOM_HEADER

## The fork/exec flow in detail

    1. Create two pipes:
       pipe(stdin_pipe)   → child reads request body from here
       pipe(stdout_pipe)  → parent reads CGI output from here

    2. Fork:
       pid = fork()

    3. In the CHILD process:
       - close unused pipe ends
       - dup2(stdin_pipe[0],  STDIN_FILENO)
       - dup2(stdout_pipe[1], STDOUT_FILENO)
       - close original pipe fds
       - chdir() to the script directory
       - execve(cgi_path, argv, envp)
       - if execve fails: write error to stderr and _exit(1)
       - NEVER call exit() in child — use _exit() to avoid
         flushing parent's stdio buffers

    4. In the PARENT process:
       - close unused pipe ends
       - write request.body into stdin_pipe[1]
       - close stdin_pipe[1] to signal EOF to the child
       - read from stdout_pipe[0] into a string
       - waitpid() to reap the child — avoid zombies
       - parse the CGI output
       - write final response into write_buffer

## Parsing CGI output

CGI programs output headers followed by a blank line then body.
The headers are NOT a full HTTP response — they look like:

    Content-Type: text/html\r\n
    Status: 200 OK\r\n        ← optional, defaults to 200
    \r\n
    <body>

Your parser must:
    1. Read until \r\n\r\n to split headers from body
    2. Parse the Status header for the status code
    3. Build a proper HTTP/1.1 response with those headers
    4. Append Content-Length based on body size
    5. Write into write_buffer

## CGI timeout handling

CGI scripts can hang. You MUST implement a timeout:

    After fork(), record the start time.
    In your read loop, check elapsed time.
    If elapsed > cgi_timeout (e.g. 10 seconds):
        kill(pid, SIGKILL)
        waitpid()
        sendError(504 Gateway Timeout)

## What to test after Phase 4

    - GET request to a Python CGI script → correct dynamic output
    - POST request with body to a CGI script → body available in stdin
    - CGI script with QUERY_STRING → env variable correct
    - CGI script that takes > timeout → 504 response
    - CGI script that returns a non-200 Status header → correct code
    - CGI script that outputs cookies → Set-Cookie header preserved

---

# 6. Phase 5 — Wiring Everything Together in main()

## What main() must do

    1. Parse command line arguments → config file path
    2. Parse config file → std::vector<ServerConfig>
    3. Create EventLoop
    4. For each ServerConfig: create and bind a server socket
    5. Register each socket with EventLoop::addServerSocket()
    6. Install signal handlers (SIGINT, SIGTERM → call loop.stop())
    7. Call loop.run() — blocks until stopped
    8. Clean shutdown — EventLoop destructor handles the rest

## Creating and binding a server socket

For each ServerConfig:

    1. socket(AF_INET, SOCK_STREAM, 0)
    2. setsockopt(SO_REUSEADDR) → avoid "Address already in use" on restart
    3. bind() to config.host:config.port
    4. listen() with a backlog (e.g. 128)
    5. EventLoop::setNonBlocking(fd)
    6. loop.addServerSocket(fd, &config)

## Signal handling

Define a global volatile sig_atomic_t g_running = 1.
In the signal handler set g_running = 0.
In main() loop check g_running or use it to call loop.stop().

Do NOT do complex work in signal handlers.
The handler sets a flag, main() acts on it.

## Multiple ports

If you have multiple ServerConfig blocks on different ports,
each gets its own server socket. Multiple configs on the SAME port
share one socket — the EventLoop dispatches based on the Host header
after the Connection is established and the request is parsed.

## Plugging in the real handlers

EventLoop currently uses stub methods. You need to:

    1. Add HttpParser     as a member of EventLoop (or Connection)
    2. Add ResponseHandler as a member of EventLoop
    3. Replace _stubParse() call with:
       _parser.feed(conn->readBuffer(), conn->request());
    4. Replace _stubBuildResponse() call with:
       _responder.handle(conn->request(), *conn->config(),
                         conn->writeBuffer());
    5. Replace _stubSend400() call with:
       _responder.sendError(400, *conn->config(),
                            conn->writeBuffer());

## What to test after Phase 5

    - Server starts with a valid config file
    - Server starts with multiple virtual hosts
    - SIGINT causes clean shutdown — no fd leaks
    - Two browsers connect simultaneously — both served correctly
    - One slow client does not block others (non-blocking confirmed)

---

# 7. Phase 6 — Testing and Hardening

## Tools to use

    curl        → send precise HTTP requests, inspect responses
    telnet      → send raw bytes, test partial requests
    siege       → load testing, concurrent connections
    valgrind    → memory leak detection
    strace      → syscall tracing, diagnose fd leaks
    wireshark   → see exactly what bytes go over the wire

## Correctness tests (do these first)

    curl -v http://localhost:8080/
        → 200 with index.html content

    curl -v http://localhost:8080/missing
        → 404 with error page

    curl -X POST -d "name=test" http://localhost:8080/cgi-bin/test.py
        → CGI output

    curl -X DELETE http://localhost:8080/uploads/file.txt
        → 204 if file existed

    curl -v http://localhost:8080/redirect
        → 301 with Location header

    curl --max-time 5 http://localhost:8080/slow-cgi
        → 504 if CGI times out

## HTTP compliance tests

    Send HTTP/1.0 request → server responds 1.0, closes connection
    Send HTTP/1.1 request → server responds 1.1, keeps alive
    Send request with no Host header in HTTP/1.1 → 400
    Send request body larger than client_max_body_size → 413
    Send unknown method → 405
    Send request to wrong virtual host → served by default server

## Edge cases to test explicitly

    Partial request arrival:
        Use telnet, type GET / HTTP/1.1 then wait 5 seconds
        Type Host: localhost then wait
        Type \r\n\r\n to complete
        → Server should wait patiently and serve correctly

    Very large file download:
        Serve a 100MB file
        Verify it arrives complete and uncorrupted (md5sum)

    Pipelined requests (HTTP/1.1):
        Send two complete requests back to back without waiting
        Both should be served in order

    Connection limit:
        Open 1000 simultaneous connections
        Server should handle them without crashing

    Zero-byte body:
        POST with Content-Length: 0
        Body in HttpRequest should be empty string

    Chunked upload:
        Send a POST with Transfer-Encoding: chunked
        Verify body is correctly reassembled

## Memory and resource tests

    Run server under valgrind:
        valgrind --leak-check=full ./webserv config.conf

    Connect 100 clients then disconnect them all abruptly:
        No fd leaks (check /proc/self/fd or lsof)
        No memory leaks reported by valgrind

    Run for 24 hours under light load:
        Memory usage should not grow (no slow leaks)

    Kill the server with SIGKILL (unclean):
        Restart immediately → SO_REUSEADDR allows instant rebind

## Performance baseline

Using siege or wrk:

    wrk -t4 -c100 -d30s http://localhost:8080/index.html

    Measure:
        Requests per second   (target: > 1000 on a modern machine)
        Latency p99           (target: < 100ms under load)
        Error rate            (target: 0%)

    If performance is below target, profile with gprof or perf
    and look at: buffer copies, string allocations, map lookups.

---

# Final Checklist Before Submission

    [ ] Config file parsing works for all required directives
    [ ] Virtual host matching works (Host header)
    [ ] Location matching uses longest prefix
    [ ] GET, POST, DELETE implemented and tested
    [ ] Static file serving with correct MIME types
    [ ] Directory listing (autoindex) works
    [ ] Custom error pages work
    [ ] Redirects work
    [ ] CGI execution works for GET and POST
    [ ] CGI timeout kills the child process
    [ ] Chunked transfer encoding decoded correctly
    [ ] client_max_body_size enforced → 413
    [ ] Keep-alive works correctly
    [ ] Timeout closes idle connections
    [ ] No memory leaks under valgrind
    [ ] No fd leaks under load
    [ ] Server survives SIGINT cleanly
    [ ] Multiple simultaneous clients work
    [ ] Server does not block on any single client
    [ ] All error responses have correct status codes

---

# Quick Reference — File Map

    Buffer.hpp              → dynamic byte buffer
    HttpRequest.hpp         → parsed request data bag + ParseState enum
    ConnectionState.hpp     → CS_READING/PROCESSING/WRITING/CLOSING enum
    Connection.hpp          → one live TCP connection, owns buffers
    ConnectionManager.hpp   → owns the connections map, enforces lifecycle
    EventLoop.hpp           → epoll dispatch loop, calls all handlers

    TO BE WRITTEN:
    ServerConfig.hpp        → config data + location matching
    ConfigParser.hpp        → reads config file → vector<ServerConfig>
    HttpParser.hpp          → fills HttpRequest from Buffer
    ResponseHandler.hpp     → builds HTTP response into Buffer
    CgiHandler.hpp          → forks CGI process, reads output
    MimeTypes.hpp           → extension → Content-Type map
    Utils.hpp               → trim, split, itoa, http date helpers
    main.cpp                → binds sockets, starts EventLoop