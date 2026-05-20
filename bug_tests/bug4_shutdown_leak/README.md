# BUG 4 — Client EventRef and CgiJob Objects Leaked on Shutdown

## Location
`src/EventLoop.cpp` — method `stop()`

## Root Cause

`stop()` only frees `EventRef` objects for **server** fds:
```cpp
void EventLoop::stop() {
    _running = false;
    for (size_t i = 0; i < _server_fds.size(); ++i) {
        int fd = _server_fds[i];
        if (fd >= 0) {
            epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            if (_event_refs.count(fd) && _event_refs[fd] != NULL) {
                delete _event_refs[fd];   // ← ONLY server EventRefs freed
                _event_refs.erase(fd);
            }
        }
    }
    // _event_refs still holds ALL client EventRef* and CGI fd EventRef*
    // _cgi_jobs and _cgi_stdin_jobs hold all CgiJob*
    // None of them are deleted here.
    ...
}
```

After `stop()`, the destructor `~EventLoop()` calls `delete _manager` which closes
all client fds — but by then `_event_refs`, `_cgi_jobs`, and `_cgi_stdin_jobs` are
still populated with live heap pointers that are never freed.

## Leaking objects

| Container | Type | Notes |
|-----------|------|-------|
| `_event_refs` | `EventRef*` | One per client fd + one per CGI result fd + one per CGI stdin fd |
| `_cgi_jobs` | `CgiJob*` | One per in-flight CGI request |
| `_cgi_stdin_jobs` | `CgiJob*` | Same `CgiJob*` aliased by stdin fd (double counted by Valgrind) |

## How to Trigger / Confirm

This bug is confirmed with Valgrind. The trigger script:
1. Connects clients and makes CGI requests (creates EventRefs and CgiJobs).
2. Sends SIGINT to the server.
3. Valgrind reports "definitely lost" blocks from `EventLoop::_registerEventFd`
   and `EventLoop::_startCgi`.

Run `./run_test.sh` which automates the full sequence.

## Expected Valgrind Output (bug present)

```
==PID== HEAP SUMMARY:
==PID==     in use at exit: N bytes in M blocks
==PID==
==PID== N bytes in M blocks are definitely lost in loss record ...
==PID==    at 0x...: operator new(unsigned long)
==PID==    by 0x...: EventLoop::_registerEventFd(int, EventKind, unsigned int)
==PID==    by 0x...: EventLoop::_handleAccept(int)
==PID==    ...
==PID==
==PID== N bytes in M blocks are definitely lost in loss record ...
==PID==    at 0x...: operator new(unsigned long)
==PID==    by 0x...: EventLoop::_startCgi(...)
```

## Expected Valgrind Output (bug fixed)
```
==PID== All heap blocks were freed -- no leaks are possible
```

## Fix

Add to `EventLoop::stop()` after the server-fd loop:
```cpp
// Free all remaining client/CGI EventRefs
for (std::map<int,EventRef*>::iterator it = _event_refs.begin();
     it != _event_refs.end(); ++it)
    delete it->second;
_event_refs.clear();

// Free all CgiJob objects
// (_cgi_stdin_jobs holds aliased pointers — skip to avoid double-free)
for (std::map<int,CgiJob*>::iterator it = _cgi_jobs.begin();
     it != _cgi_jobs.end(); ++it)
    delete it->second;
_cgi_jobs.clear();
_cgi_stdin_jobs.clear();
```
