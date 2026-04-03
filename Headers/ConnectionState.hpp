// ============================================================
//  ConnectionState.hpp
//  Enum that drives the Connection state machine.
//  C++98 compliant — lives entirely in this header.
//
//  Every epoll event maps directly to one of these states.
//  Connection checks its state to decide what to do next,
//  and epoll registration (EPOLLIN vs EPOLLOUT) follows it.
//
//  State transition diagram:
//
//                     accept()
//                        │
//                        ▼
//                   ┌─────────┐
//            ┌─────▶│ READING │◀────────────────┐
//            │      └────┬────┘                 │
//            │           │ EPOLLIN fired         │
//            │           │ read → parse          │
//            │           │                       │
//            │     [parse_state                  │
//            │      == PS_COMPLETE]              │
//            │           │                       │
//            │           ▼                       │
//            │      ┌──────────┐                 │
//            │      │PROCESSING│                 │
//            │      └────┬─────┘                 │
//            │           │ response built        │
//            │           │ into write_buffer     │
//            │           ▼                       │
//            │      ┌─────────┐                  │
//            │      │ WRITING │                  │
//            │      └────┬────┘                  │
//            │           │ EPOLLOUT fired        │
//            │           │ drain write_buffer    │
//            │           │                       │
//            │    [write_buffer empty]            │
//            │           │                       │
//            │    ┌──────┴──────┐                │
//            │    │             │                │
//            │  [keep-alive]  [close]            │
//            └────┘             │                │
//           reset()             ▼                │
//                          ┌─────────┐           │
//                          │ CLOSING │           │
//                          └────┬────┘           │
//                               │                │
//                    EPOLLERR / EPOLLHUP ─────────┘
//                    closeConnection()
//                               │
//                               ▼
//                          [destroyed]
//
// ============================================================
#ifndef CONNECTION_STATE_HPP
#define CONNECTION_STATE_HPP

// ────────────────────────────────────────────────────────────
//  ConnectionState
//
//  READING     The connection is waiting for / receiving data.
//              epoll watches EPOLLIN.
//              On event : recv() → append to read_buffer
//                       → Parser runs
//                       → if PS_COMPLETE → transition to PROCESSING
//
//  PROCESSING  A complete request is in HttpRequest.
//              No epoll event needed at this stage —
//              the server loop calls processRequest() directly.
//              On finish : response built into write_buffer
//                        → transition to WRITING
//                        → epoll re-armed with EPOLLOUT
//
//  WRITING     Response bytes are in write_buffer, being drained.
//              epoll watches EPOLLOUT.
//              On event : send() from write_buffer
//                       → consume() sent bytes
//                       → if write_buffer empty:
//                           keep-alive → reset → READING
//                           close      → CLOSING
//
//  CLOSING     Connection is done. No further I/O.
//              epoll watches nothing (fd will be removed).
//              closeConnection() deletes the Connection object
//              and erases it from the map.
//              Triggered by:
//                - EPOLLERR / EPOLLHUP at any state
//                - write_buffer drained + no keep-alive
//                - parse error (PS_ERROR) → send 400 → CLOSING
//                - timeout (last_active too old)
// ────────────────────────────────────────────────────────────
enum ConnectionState
{
    CS_READING    = 0,  ///< receiving request data        (EPOLLIN)
    CS_PROCESSING,      ///< building the response         (no epoll)
    CS_WRITING,         ///< draining write buffer         (EPOLLOUT)
    CS_CLOSING          ///< connection is done, tear down (no epoll)
};


// ────────────────────────────────────────────────────────────
//  stateToString()
//  Debug / logging helper.
//  Returns a human-readable label for a ConnectionState value.
//  Safe to call from anywhere — no side effects.
//
//  Usage:
//    std::cerr << "[fd " << fd << "] state → "
//              << stateToString(state) << "\n";
// ────────────────────────────────────────────────────────────
inline const char* stateToString(ConnectionState s)
{
    switch (s)
    {
        case CS_READING:    return "READING";
        case CS_PROCESSING: return "PROCESSING";
        case CS_WRITING:    return "WRITING";
        case CS_CLOSING:    return "CLOSING";
        default:            return "UNKNOWN";
    }
}

#endif // CONNECTION_STATE_HPP