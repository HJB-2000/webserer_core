# Webserv — Bug Test Suite

Each directory targets one bug from **Part 3 (Crashes / Undefined Behaviour)**
of `replit_bugs_report.md`.

## Quick Start

```bash
# Run a single bug test (starts server, fires trigger, reports result)
chmod +x bug_tests/bug*/run_test.sh
./bug_tests/bug3_epollout_dropped/run_test.sh      # easiest to reproduce
./bug_tests/bug7_chunked_crlf/run_test.sh          # deterministic pass/fail
```

## Directory Overview

| Directory | Bug | Trigger method | Difficulty |
|-----------|-----|----------------|------------|
| `bug1_uaf_startcgi/` | Use-after-free when `epoll_ctl(MOD)` fails after CGI starts | RST race + code injection patch | Hard (race condition) |
| `bug2_eventref_leak/` | `EventRef*` leaked when `epoll_ctl(ADD)` fails | Exhaust epoll watch limit + Valgrind | Medium |
| `bug3_epollout_dropped/` | EPOLLOUT dropped on simultaneous EPOLLIN+EPOLLOUT | Pipelined request over large response | Medium |
| `bug4_shutdown_leak/` | All client EventRef and CgiJob objects leaked on shutdown | Valgrind leak check | Easy (Valgrind confirms) |
| `bug5_ipv4_cast/` | Wrong REMOTE_ADDR when IPv6-mapped IPv4 connection arrives | IPv6 loopback connection | Easy (if IPv6 available) |
| `bug6_const_cast/` | `const_cast` mutates `req.path` in-place via const-ref | UBSan build + path traversal request | Easy (UBSan confirms) |
| `bug7_chunked_crlf/` | Chunked trailing CRLF consumed without validation | Malformed chunked POST | Easy (deterministic 400 vs 2xx) |

## Prerequisites

```bash
# Python 3 (for trigger.py scripts)
python3 --version

# Valgrind (for bugs 2 and 4)
valgrind --version   # apt-get install valgrind

# The server must be built
make re
```

## Files in Each Directory

```
bugN_name/
├── README.md      Full bug description, code snippet, fix suggestion
├── trigger.py     Python script that demonstrates the bug
└── run_test.sh    Shell wrapper: builds server, runs trigger, shows result
```

Bug 1 also includes `inject_patch.diff` — a deterministic code-injection patch
that forces the `epoll_ctl(MOD)` failure needed to trigger the UAF.

## Recommended Test Order

Start with the deterministic bugs (7, 6, 4) before the race-condition ones (1, 3):

```bash
./bug_tests/bug7_chunked_crlf/run_test.sh     # pass/fail in seconds
./bug_tests/bug6_const_cast/run_test.sh       # UBSan confirms immediately
./bug_tests/bug4_shutdown_leak/run_test.sh    # Valgrind shows leaks
./bug_tests/bug3_epollout_dropped/run_test.sh # may need multiple runs
./bug_tests/bug2_eventref_leak/run_test.sh    # needs Valgrind
./bug_tests/bug5_ipv4_cast/run_test.sh        # needs IPv6
./bug_tests/bug1_uaf_startcgi/run_test.sh     # race — apply inject_patch for certainty
```
