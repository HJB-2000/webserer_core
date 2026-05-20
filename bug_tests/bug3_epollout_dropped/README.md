# BUG 3 — EPOLLOUT Silently Dropped When EPOLLIN and EPOLLOUT Arrive Together

## Location
`src/EventLoop.cpp` — method `_handleClientEvent(int client_fd, uint32_t events)`

## Root Cause

```cpp
void EventLoop::_handleClientEvent(int client_fd, uint32_t events)
{
    ...
    if (events & EPOLLIN)
    {
        _handleRead(conn);
        return;      // ← EARLY RETURN — EPOLLOUT bit is silently discarded
    }
    if (events & EPOLLOUT)
    {
        _handleWrite(conn);
        return;
    }
}
```

In edge-triggered (EPOLLET) mode the kernel coalesces events. When data arrives
from the client AND the kernel socket send-buffer has space simultaneously, a
single epoll_wait event is delivered with `events = EPOLLIN | EPOLLOUT`.

The early `return` after `_handleRead` means the EPOLLOUT bit is never acted on.
Because epoll is edge-triggered, no new EPOLLOUT event will fire until more space
appears in the send buffer — but the response is already fully assembled and
waiting in the write-buffer. The connection **stalls permanently**.

## How to Trigger

The most reliable way is to:
1. Request a large static file (fills the kernel send buffer → EPOLLOUT needed).
2. While the response is being sent (buffer still full), immediately send a second
   pipelined request on the same keep-alive connection (EPOLLIN arrives).
3. Both bits appear together in the next epoll_wait.
4. The server reads the pipelined request (handles EPOLLIN) but never sends the
   rest of the first response (EPOLLOUT dropped) → read hangs.

`trigger.py` does exactly this using:
- A first request for the largest static file on the server.
- Immediate back-to-back pipelined request before reading the response.
- A timeout to detect the hang.

## Expected Result (bug present)
`trigger.py` prints:
```
[STALL DETECTED] Second response never arrived (timeout=5s).
Connection is hung — EPOLLOUT was dropped.
```

## Expected Result (bug fixed)
Both responses arrive within the timeout window.

## Manual Reproduction with curl

```bash
# Generate a large static file to stress the send buffer
dd if=/dev/urandom bs=1M count=4 | base64 > www/html/bigfile.txt

# Start the server
./webserv conf/confs/replit.conf &

# Send two pipelined requests — curl --http1.1 keeps the connection alive
# If the second response never arrives, the bug is confirmed.
curl -v --http1.1 \
     http://127.0.0.1:5000/bigfile.txt \
     http://127.0.0.1:5000/index.html \
     --max-time 5
```
If the command hangs after printing the first response headers, the bug is present.
