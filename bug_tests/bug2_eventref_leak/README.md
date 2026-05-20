# BUG 2 — Memory Leak in `_registerEventFd` When `epoll_ctl ADD` Fails

## Location
`src/EventLoop.cpp` — method `_registerEventFd(int fd, EventKind kind, uint32_t events)`

## Root Cause

```cpp
ref = new EventRef(kind, fd);
_event_refs[fd] = ref;          // ← stored in map FIRST

if (::epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0)
    throw std::runtime_error(...);   // ← ref leaks — stays in _event_refs forever
```

When `epoll_ctl(ADD)` fails, the exception propagates to the caller (`_addCgiFd`).
That caller closes the fd:
```cpp
catch (const std::exception& e) {
    ::close(result_fd);
    throw;
}
```
But `_unregisterEventFd` is never called, so `_event_refs[result_fd]` holds a
dangling `EventRef*` that is never deleted.

## When does `epoll_ctl(ADD)` fail?

- `ENOSPC`: per-user epoll watch limit reached (`/proc/sys/fs/epoll/max_user_watches`)
- `EBADF`:  the fd was already closed before registration
- `ENOMEM`: kernel memory exhausted

## How to Trigger

### Method 1 — Exhaust `max_user_watches` (cleanest)

The script `trigger.py` opens many idle connections with open CGI responses,
exhausting the epoll watch budget. The next CGI fd registration then fails.
Run with Valgrind to see the leak report.

### Method 2 — Lower system limit manually

```bash
# Save current limit
cat /proc/sys/fs/epoll/max_user_watches

# Lower it to just above baseline (adjust number based on your system)
echo 100 | sudo tee /proc/sys/fs/epoll/max_user_watches

# Run the server and make a few CGI requests
./webserv conf/confs/replit.conf &
curl http://127.0.0.1:5000/cgi-bin/check_session.py

# Restore
echo 524288 | sudo tee /proc/sys/fs/epoll/max_user_watches
```

### Method 3 — Valgrind confirmation (recommended)

```bash
make re
valgrind --leak-check=full --track-origins=yes \
         ./webserv conf/confs/replit.conf &
VPID=$!
python3 trigger.py
sleep 2
kill $VPID
# Valgrind report appears on stderr — look for "definitely lost" from EventRef
```

## Expected Output (Valgrind, bug present)

```
==PID== LEAK SUMMARY:
==PID==    definitely lost: 24 bytes in 1 blocks
==PID==    ...
==PID== 24 bytes in 1 blocks are definitely lost ...
==PID==   at 0x...: operator new(unsigned long)
==PID==   by 0x...: EventLoop::_registerEventFd(int, EventKind, unsigned int)
```

## Expected Output (bug fixed)
Valgrind reports 0 bytes definitely lost related to EventRef.
