# BUG 1 — Use-After-Free in `_startCgi`

## Location
`src/EventLoop.cpp` — method `_startCgi(Connection* conn, const CgiRequestInfo& info)`

## Root Cause

```
conn->setCgiRunning();
_rearmClient(conn->fd());          ← (A) can call _manager->closeConnection(fd)
                                       which calls `delete conn`

// CGI fork happens here ...

if (!ok) {
    _responder.sendError(500, *conn->config(), ...);  ← UAF: conn is deleted
    conn->setWriting();                               ← UAF
    _rearmClient(conn->fd());                         ← UAF
}
```

The call chain that frees `conn`:
```
_rearmClient(fd)
  → _modifyEventFd(fd, EV_CLIENT, events)
    → epoll_ctl(MOD) fails (returns -1)
      → _manager->closeConnection(fd)
        → delete conn          ← conn is now freed
```

After (A), the `!ok` branch dereferences freed memory → SIGSEGV.

## When does `epoll_ctl(MOD)` fail?

`epoll_ctl` returns -1 / EBADF when the fd has already been closed
(e.g. the client disconnected between the read event and `_startCgi`).
It also fails with ENOSPC if the epoll watch limit is hit.

## How to trigger

### Method 1 — Race with client disconnect (most realistic)

Send a CGI request and close the connection immediately at the TCP level
before the server calls `_rearmClient`. The server's `epoll_ctl(MOD)` then
operates on an fd that epoll no longer tracks → returns -1.

Run `trigger.py` which sends a CGI request on a raw socket and does a
hard RST (`SO_LINGER, l_onoff=1, l_linger=0`) immediately after sending
the request headers.

### Method 2 — Exhaust the epoll watch limit

```bash
# Lower the limit, then connect many clients
ulimit -n 32
python3 trigger.py --exhaust
```

### Method 3 — Code-injection patch (deterministic)

Apply `inject_patch.diff` which adds a forced failure in `_modifyEventFd`
for the first CGI request. Recompile and run any CGI request.

## Expected result (bug present)
Server crashes with SIGSEGV or produces garbled output.
In ASan build: `ERROR: AddressSanitizer: heap-use-after-free`.

## Expected result (bug fixed)
Server returns a 500 error to the client cleanly and continues running.

## How to confirm with AddressSanitizer

```bash
# Recompile with ASan
make re CXXFLAGS="-g3 -std=c++98 -Wall -Wextra -fsanitize=address,undefined"
./webserv conf/confs/replit.conf &
python3 trigger.py
```
