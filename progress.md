# Core

## DONE

- [x] Non-blocking listener setup.
- [x] Epoll-based event loop tick for dispatch.
- [x] Multi-client accept handling in edge-trigger mode.
- [x] Non-blocking read drain per client until EAGAIN.
- [x] Client tracking and close path cleanup.
- [x] Resource cleanup for clients, listener fd, epoll fd, and addrinfo.
- [x] Write path handling with output buffers and EPOLLOUT flow.
- [x] Clear connection state transitions (read/process/write/close).
- [x] Timeout and idle-client eviction policy.
- [x] Minimal protocol behavior (request boundary + basic HTTP response).
- [x] Stronger socket and epoll error/hangup handling.
- [x] Added parser/response integration entry-point markers in core methods.
- [x] Added temporary main-loop staging break for phased rollout.
- [x] Added dual listener support (IPv4 + IPv6 when available).
- [x] Registered all listener sockets in epoll dispatch path.

## TODO

- [ ] Add repeatable multi-client runtime validation tests.
- [ ] Verify strict C++98 build passes cleanly in local terminal run.
- [ ] Reduce hot-path syscall overhead where possible (accept/recv/send/epoll_ctl).
- [ ] Validate EPOLLOUT toggling efficiency and avoid unnecessary epoll MOD calls.
- [ ] Reduce buffer copy overhead in read/write paths after parser integration.
- [ ] Re-evaluate connection lookup structure cost under high fd counts.
- [ ] Optimize timeout sweep strategy to reduce O(n) scan impact at scale.
- [ ] Minimize/disable hot-path logging in execution mode.
