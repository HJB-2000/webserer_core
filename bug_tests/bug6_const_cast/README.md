# BUG 6 — `const_cast` Mutation of a `const`-Reference Parameter

## Location
`src/ResponseHandler.cpp` — `ResponseHandler::handle()`

## Root Cause

```cpp
void ResponseHandler::handle(const HttpRequest& req,
                              const ServerConfig& cfg,
                              Buffer& wb)
{
    // ... path sanitisation logic ...
    std::string safe_path = /* sanitised version of req.path */;

    const_cast<HttpRequest&>(req).path = safe_path;   // ← mutates caller's object
```

`handle()` declares `req` as `const HttpRequest&` — a promise to the caller
that the request will not be modified. The `const_cast` silently breaks this
promise by writing directly to `Connection::_request.path` (the object the
reference points to).

## Why this matters

**Standard §7.1.6.1:** Modifying a const-declared object through a `const_cast`
is undefined behaviour. While the `HttpRequest` object is not itself declared
`const` (it's a non-const member of `Connection`), the code creates a maintenance
trap:

1. **Silent state mutation:** After `handle()` returns, `conn->request().path`
   is different from what was received from the client. If the path is logged,
   stored, or used after the call, it reflects the sanitised version without any
   indication this happened.

2. **Keep-alive pipeline corruption:** On a keep-alive connection, `setReading()`
   calls `reset()` which resets `path`. This hides the mutation in the common
   path — but any code between `handle()` returning and `reset()` being called
   sees the modified path. Adding a log line or assertion between those two points
   would produce unexpected results.

3. **Compiler aliasing breakage:** With `-O2`, the compiler can legally cache
   `req.path` in a register across the `const_cast` write, making the mutation
   invisible to subsequent reads of `req.path` in the same function.

## How to Trigger

### Test 1 — Path traversal triggers the const_cast

Send a URL with `../` — the sanitiser replaces it with `/`:
```
GET /../../etc/passwd HTTP/1.1
```
After `handle()`, `conn->request().path` will be `/etc/passwd` (or `/`),
not `/../../etc/passwd`. `trigger.py` confirms this by checking what path
the server logs (stderr) vs what the client sent.

### Test 2 — Compiler optimisation breakage

Compile with `-O2` and `const_cast` sanitiser:
```bash
make re CXXFLAGS="-g3 -std=c++98 -Wall -Wextra -Werror -O2 \
                  -fsanitize=undefined"
```
UBSan will flag: `store to address ... which is an lvalue of type const ...`

### Test 3 — AddressSanitizer

```bash
make re CXXFLAGS="-g3 -std=c++98 -fsanitize=address,undefined"
./webserv conf/confs/replit.conf &
python3 trigger.py
```

## Expected Result (bug present)
Server stderr shows the sanitised path, not the original requested path.
With UBSan: runtime error on the const_cast line.

## Fix

Store the sanitised path in a local variable:
```cpp
void ResponseHandler::handle(const HttpRequest& req, ...) {
    std::string safe_path = _sanitisePath(req.path);  // local — not written back
    // use safe_path everywhere below instead of req.path
}
```
