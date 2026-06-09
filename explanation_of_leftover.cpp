/*
Here's the full picture of what happens, from tester stop to the stuck RSS number.

Phase 1: Normal operation (before you stop the tester)
20 clients connect and send 100MB POST bodies to the CGI endpoint. For each client:

_handleRead feeds the parser until PSTATE_COMPLETE
_ApiStartCgi creates a CgiJob, registers result_fd in _cgi_jobs and stdin_fd in _cgi_stdin_jobs
The body sits in conn->request().body (a Buffer backed by a std::vector<char>)
_handleCgiStdinEvent drains that body into the child's stdin pipe chunk by chunk

At peak, all 20 bodies are simultaneously resident in memory. Each Buffer::_storage vector holds ~100MB of heap-allocated pages. glibc's allocator requested those pages from the kernel via mmap (for large allocations) or sbrk (for the main arena). RSS climbs to ~1940 MB.

Phase 2: You stop the tester (Ctrl+C on the tester side)
The tester process exits. The kernel immediately sends TCP RST packets to all 20 open connections. Your server's epoll sees events on all 20 client fds almost simultaneously.
First wave — "Connection reset by peer":
Some clients are mid-_handleWrite when epoll fires EPOLLOUT. conn->send() returns -1 with errno == ECONNRESET. The path is:
_handleWrite → _closeClient(fd)
    → _closeCgiJobsForClient(fd)   // finds the CgiJob, calls _closeCgiJob
        → _closeCgiStdin(job)      // closes stdin pipe, child gets EOF
        → kill(child_pid, SIGKILL)
        → _unregisterEventFd(result_fd)
        → ::close(result_fd)
        → delete job               // CgiJob freed
    → _unregisterEventFd(fd)       // EventRef moved to _stale_refs
    → _manager->closeConnection(fd)
        → delete conn              // Connection destructs:
                                   //   ~Buffer() runs on _read_buffer,
                                   //   _write_buffer, request().body
                                   //   vector::~vector calls free()
free() returns the 100MB chunks to glibc's allocator. The kernel does not get the pages back yet. glibc marks those chunks as free in its internal bins but keeps the arena mapped — it's betting you'll allocate again soon.
RSS drops by ~95MB per client that closes this way.
Second wave — "error/hup":
The remaining clients hit EPOLLERR | EPOLLHUP → _handleError → same _closeClient path. Same chain, same result: bodies freed at C++ level, pages retained by glibc.

Phase 3: After all connections close — the stuck RSS
All 20 Connection objects are deleted. All 20 Buffer::_storage vectors are destroyed. All the memory is logically free from C++'s perspective.
But RSS is still showing ~294 MB (or ~530 MB in the other run). This is entirely glibc's allocator behavior:

For allocations larger than MMAP_THRESHOLD (default 128KB), glibc uses mmap per allocation. When freed, those regions are returned to the kernel immediately via munmap. Your 100MB bodies exceed this threshold, so they should be returned promptly — and the big drop you see (1940 → 294) confirms most of them are.
The remaining ~294 MB is the arena overhead: the smaller allocations made during the CGI lifecycle — CgiJob structs, EventRef objects, std::map nodes for _cgi_jobs/_event_refs/_connections, HTTP header strings, response buffers, log strings. These are small allocations that go into glibc's main arena (managed via sbrk). When freed, glibc consolidates them into free chunks internally but only returns the top of the heap to the kernel if the top chunk is large enough and contiguous. Fragmented free chunks in the middle of the arena stay mapped.


Phase 4: The role of malloc_trim(0)
malloc_trim(0) tells glibc: "scan all arenas right now, and return any free pages at the top of each arena to the kernel via sbrk(-n) or madvise(MADV_DONTNEED)."
If you call it after all connections close, RSS will drop from ~294 MB toward your true baseline (~26 MB). The difference — ~268 MB — was entirely free memory sitting in glibc's arena waiting to be reused. No leak, no bug.
The reason it doesn't happen automatically is that glibc only trims proactively under specific conditions (when M_TRIM_THRESHOLD is crossed during a free() call). With 20 concurrent 100MB bodies, many of those are mmap'd individually and returned immediately on free(). But the arena high-water mark from all the smaller supporting allocations persists until either:

Another large allocation triggers a trim check
OS memory pressure forces it
You call malloc_trim(0) explicitly


Summary in one sentence
When you stop the tester, all 20 connection bodies are correctly freed through the destructor chain, the large mmap'd chunks go back to the kernel immediately (hence the big RSS drop), but ~270 MB of smaller arena allocations stay mapped in glibc's heap until malloc_trim(0) or OS pressure forces them back — this is allocator behavior, not a memory leak.
*/


/*
For _handleDelete is the correct way
    nginx typically disables DELETE by default and requires explicit configuration to enable it. When enabled, it just calls unlink the same way you do — it relies on the OS permission model, not custom permission checking.
So your implementation is correct — it matches nginx's behavior. The EACCES/EPERM cases in your errno handling would trigger if:

*/