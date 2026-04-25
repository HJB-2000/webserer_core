# CGI teammate integration contract

## The one function you must implement

Replace `src/CgiStarter.cpp` with your real implementation.
Keep the function signature exactly as declared in `Headers/CgiStarter.hpp`:

```cpp
bool startCgi(
    const HttpRequest&  req,
    const ServerConfig& cfg,
    const Location&     loc,
    const std::string&  script_path,
    int                 result_write_fd
);
```

## What core gives you

| Parameter         | What it is                                              |
|-------------------|---------------------------------------------------------|
| `req`             | Fully-parsed HTTP request (method, path, headers, body) |
| `cfg`             | Server config (root, error pages, timeouts, etc.)       |
| `loc`             | Matched location block (CGI extension, interpreter, etc.)|
| `script_path`     | Absolute filesystem path to the script to execute       |
| `result_write_fd` | Write end of a pipe — write your CGI result here        |

## What you must do

1. Fork a child process.
2. In the child: `exec` the CGI interpreter with `script_path`.
3. Collect the child's stdout (the raw CGI output).
4. Write the complete CGI output to `result_write_fd`.
5. Close `result_write_fd` when writing is done.
6. **Return from the parent immediately** — do not `wait()` or block.

## What the CGI output must look like

```
Status: 200 OK\r\n
Content-Type: text/html\r\n
\r\n
<html>...</html>
```

- `Status:` header is optional; core defaults to 200 if absent.
- `Content-Type:` is required.
- Blank line (`\r\n` or `\n`) separates headers from body.
- If the separator is missing, core returns 502 to the client.

## Critical rules

- `startCgi` must return quickly in the **parent** process.
  Do not wait for the child inside this function — that blocks the event loop.
- Write all CGI output to `result_write_fd`, then close it.
  Core detects EOF on the read end as the signal that output is complete.
- Return `true` on success, `false` if setup failed before fork.
  On `false`, core sends 500 to the client and cleans up.
- Do not close the read end of the pipe — core owns that fd.

## Environment variables to set in the child (CGI/1.1)

```
REQUEST_METHOD
QUERY_STRING
CONTENT_TYPE
CONTENT_LENGTH
SCRIPT_FILENAME    ← absolute path to the script
SERVER_NAME
SERVER_PORT
HTTP_*             ← forwarded request headers
```

Use `req`, `cfg`, `loc`, and `script_path` to populate these.

## Where core calls your function

File: `src/EventLoop.cpp`, function `EventLoop::_startCgi()`

```cpp
bool ok = startCgi(conn->request(), *conn->config(),
                   *info.location, info.script_path,
                   result_write_fd);
::close(result_write_fd);   // core closes write end in parent
```

Core already:
- Created the pipe
- Set the read end non-blocking
- Registered the read end with epoll
- Set the connection to CGI_RUNNING state

Your function just needs to fork, exec, pipe child stdout → `result_write_fd`, and return.

## Fake stub for reference (current src/CgiStarter.cpp)

```cpp
bool startCgi(
    const HttpRequest&,
    const ServerConfig&,
    const Location&,
    const std::string&,
    int result_write_fd)
{
    std::string out =
        "Status: 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "fake cgi ok\n";
    ::write(result_write_fd, out.c_str(), out.size());
    return true;
}
```

Replace this with your real fork/exec implementation.
The fake stub is only there so core compiles and the landing zone can be tested end-to-end before your code is ready.
