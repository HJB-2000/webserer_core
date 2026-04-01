# Change Tracker

## Scope
- Updated files:
	- `webserver.hpp`
	- `webserver.cpp`
- Unchanged by this step:
	- all skeleton phase files outside these two files

## Goals Of This Polish Step
- Move the core socket loop to non-blocking mode.
- Add epoll-based event handling for multiple concurrent connections.
- Fix existing logic issues in socket creation and accept flow.
- Keep the public class interface compatible (`create_socket()` and `accept_connection()`).

## `webserver.hpp` Changes
- Added headers needed for non-blocking and epoll:
	- `<sys/epoll.h>`
	- `<vector>`
	- `<fcntl.h>`
- Added constants:
	- `MAX_EVENTS`
	- `READ_BUFFER_SIZE`
- Updated private data members:
	- Removed unused connection address fields from class-level storage.
	- Added `epoll_fd`.
	- Added `std::vector<int> clients` to track active client fds.
- Replaced old helper declarations with core non-blocking helpers:
	- `set_non_blocking(int fd)`
	- `add_fd_to_epoll(int fd, uint32_t events)`
	- `remove_fd_from_epoll(int fd)`
	- `accept_all_pending()`
	- `read_from_client(int fd)`
	- `close_client(int fd)`
	- `is_client_fd(int fd) const`

## `webserver.cpp` Changes

### Constructor / Destructor
- Constructor now initializes:
	- `epoll_fd = -1`
	- `socket_fd = -1`
	- `client_socket_fd = -1`
- Destructor now safely cleans all resources:
	- closes all tracked client fds
	- closes listening fd
	- closes epoll fd
	- frees `addrinfo` if still allocated

### Socket Creation Path (`create_socket`)
- Fixed wrong addrinfo usage:
	- now uses iterator node `p` instead of always using `res`.
- Improved failure handling:
	- removed hard `exit(1)` behavior
	- closes fd and continues/fails with return code
- Listener is set to non-blocking.
- Creates epoll instance and registers listener fd with:
	- `EPOLLIN | EPOLLET`

### Setup / Address Enumeration (`setup`)
- Fixed loop increment bug (`this->p = this->p->ai_next`).
- Uses local `ipstr` buffer per iteration for clean address prints.

### New Helper Implementations
- `set_non_blocking`: uses `fcntl(F_GETFL/F_SETFL)`.
- `add_fd_to_epoll`: wraps `epoll_ctl(ADD)`.
- `remove_fd_from_epoll`: wraps `epoll_ctl(DEL)` with safe handling for `ENOENT/EBADF`.
- `is_client_fd`: validates whether fd belongs to tracked clients.
- `close_client`: removes fd from epoll, closes it, erases from vector.
- `accept_all_pending`: drains accepts in edge-trigger mode until `EAGAIN`.
- `read_from_client`: drains `recv` in non-blocking mode until `EAGAIN` or close/error.

### Event Loop Entry (`accept_connection`)
- Now runs one epoll polling cycle using `epoll_wait`.
- Dispatch behavior:
	- listener fd events -> accept all pending clients
	- client error/hangup events -> close client
	- client readable events -> drain reads

## Behavior Impact
- The server core is now non-blocking and epoll-driven.
- It can track and serve multiple simultaneous client connections.
- Edge-trigger correctness is improved by draining accept/read loops.

## Known Status
- This step focuses on connection core only (no HTTP parse/response yet).
- Main loop still calls `accept_connection()` repeatedly, which is compatible with this updated core design.

## Core Step Update: Write Path + State Machine

### `webserver.hpp` Updates
- Added internal core state machine enum inside `socket_`:
	- `CS_READING`
	- `CS_PROCESSING`
	- `CS_WRITING`
	- `CS_CLOSING`
- Added internal `Connection` struct for per-client lifecycle:
	- `fd`
	- `state`
	- `read_buffer`
	- `write_buffer`
	- `write_offset`
	- `last_active`
	- `keep_alive`
- Replaced client tracking container:
	- from `std::vector<int>` to `std::map<int, Connection>`
- Added core loop constants:
	- `EPOLL_WAIT_TIMEOUT_MS`
	- `CLIENT_IDLE_TIMEOUT_SEC`
- Added new core helper declarations:
	- `mod_fd_in_epoll`
	- `rearm_client_events`
	- `write_to_client`
	- `process_client_buffer`
	- `queue_simple_response`
	- `request_complete`
	- `should_keep_alive`
	- `sweep_idle_clients`

### `webserver.cpp` Updates
- Added epoll `MOD` support to switch client interest dynamically.
- Added event rearm logic to avoid permanent EPOLLOUT notifications:
	- `CS_WRITING` -> `EPOLLOUT`
	- otherwise -> `EPOLLIN`
- Implemented minimal request-complete check (`\r\n\r\n`).
- Implemented keep-alive decision from request headers/version.
- Added minimal response generation:
	- status line
	- content length
	- content type
	- connection header
	- body
- Implemented write path with output buffer and offset handling:
	- partial sends handled
	- EAGAIN handled
	- close-on-error handled
- Implemented state transitions:
	- read complete -> processing -> writing -> reading or closing
- Implemented idle timeout sweep and close policy.
- Strengthened error handling for:
	- `EPOLLERR`
	- `EPOLLHUP`
	- `EPOLLRDHUP`
	- recv/send failures
- Added payload-size guard (1 MB) with `413 Payload Too Large` response.

### Core Behavior Result
- Core now performs full non-blocking read + process + write cycles.
- Connections only subscribe to EPOLLOUT when response data exists.
- Idle clients are removed to protect server resources.

## Tracker Update: Integration Entry Markers + Loop Control

### `webserver.hpp`
- Added explicit integration comments on future handoff points:
	- `process_client_buffer(int fd)` marked as HttpParser entry point.
	- `queue_simple_response(...)` marked as ResponseHandler replacement point.
- Clarified helper role comments for temporary core-only request checks.

### `webserver.cpp`
- Added TODO markers at exact future hook locations:
	- start of `process_client_buffer(int fd)` for parser pipeline wiring.
	- response build area for ResponseHandler integration.
	- read path comment to indicate where parser/response plumbing begins.
- No behavior changes from these documentation markers.

### `main.cpp`
- Kept the event-loop shape and added a temporary break after one successful tick.
- This is an intentional staging control point until next phases are integrated.

## Core Update: Dual Listener Sockets (IPv4 + IPv6)

### `webserver.hpp`
- Added listener container:
	- `std::vector<int> listener_fds`
- Added listener classification helper:
	- `is_listener_fd(int fd) const`
- Core class now models one-or-more listening sockets instead of a single listener assumption.

### `webserver.cpp`
- Updated `create_socket()` to attempt both families and keep up to:
	- one IPv4 listener
	- one IPv6 listener
- Added IPv6 socket option setup with `IPV6_V6ONLY` for predictable dual-listener behavior.
- Moved `listen()` and epoll `ADD` handling to run for each successfully created listener.
- Updated event dispatch in `accept_connection()`:
	- listener detection now checks against registered listener set
	- accept path works for whichever listener fd fired
- Updated destructor cleanup to close all listener fds.

### Behavior Impact
- Core can now accept IPv4 and IPv6 clients concurrently when both listeners are available.
- Event loop remains single-threaded and non-blocking while handling multiple server fds.
