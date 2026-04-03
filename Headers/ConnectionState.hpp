// ============================================================
//  ConnectionState.hpp
//  Enum that drives the Connection state machine.
//  C++98 compliant.
//
//  Implementation: src/ConnectionState.cpp  (connStateStr)
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

enum ConnectionState
{
    CSTATE_READING    = 0,  ///< receiving request data        (EPOLLIN)
    CSTATE_PROCESSING,      ///< building the response         (no epoll)
    CSTATE_WRITING,         ///< draining write buffer         (EPOLLOUT)
    CSTATE_CLOSING          ///< connection is done, tear down (no epoll)
};

// Debug / logging helper — returns human-readable state label.
const char* connStateStr(ConnectionState s);

#endif // CONNECTION_STATE_HPP
