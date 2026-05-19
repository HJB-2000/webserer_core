#!/usr/bin/env python3
"""
BUG 2 — Memory Leak in _registerEventFd
=========================================
Strategy:
  1. Open a large number of idle connections (to consume epoll watches).
  2. For each idle connection, trigger a CGI response that creates a CGI
     result pipe (another 2 watches: result_fd + stdin_fd).
  3. When max_user_watches is hit, the next epoll_ctl(ADD) fails,
     triggering the leak.
  4. Valgrind will report the leaked EventRef.

Usage:
    # Terminal 1 — start server under valgrind
    valgrind --leak-check=full --track-origins=yes \\
             ./webserv conf/confs/replit.conf

    # Terminal 2 — run this trigger
    python3 trigger.py [--host 127.0.0.1] [--port 5000]

    # Terminal 1 — Ctrl+C the server, valgrind report appears.

Note:
    On systems where lowering max_user_watches requires root, use Method 2
    from README.md (sudo echo 100 > /proc/sys/fs/epoll/max_user_watches).
"""

import socket
import time
import argparse
import sys
import threading

IDLE_CONNECTIONS = []
LOCK = threading.Lock()

def open_idle_connection(host, port):
    """Open a TCP connection and keep it open (do not send anything)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    try:
        s.connect((host, port))
        with LOCK:
            IDLE_CONNECTIONS.append(s)
        return True
    except Exception as e:
        return False

def send_cgi_request(host, port):
    """Send a CGI request — causes server to call _registerEventFd for CGI pipe."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(3)
    try:
        s.connect((host, port))
        req = (
            "GET /cgi-bin/check_session.py HTTP/1.1\r\n"
            "Host: {}\r\n"
            "Connection: close\r\n"
            "\r\n"
        ).format(host)
        s.sendall(req.encode())
        response = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            response += chunk
        s.close()
        return response
    except Exception as e:
        s.close()
        return b""

def check_server_alive(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(1.0)
    try:
        s.connect((host, port))
        s.close()
        return True
    except Exception:
        return False

def main():
    parser = argparse.ArgumentParser(description="BUG 2 trigger — EventRef leak")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--idle-conns", type=int, default=200,
                        help="Number of idle connections to park (default 200)")
    parser.add_argument("--cgi-rounds", type=int, default=30,
                        help="Number of CGI requests to trigger after parking")
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 2 — Memory Leak in _registerEventFd")
    print("=" * 60)
    print(f"Target     : {args.host}:{args.port}")
    print(f"Idle conns : {args.idle_conns}")
    print(f"CGI rounds : {args.cgi_rounds}")
    print()
    print("NOTE: Run the server under valgrind first — see README.md")
    print()

    if not check_server_alive(args.host, args.port):
        print("[!] Server is not responding. Start it first.")
        sys.exit(1)

    # Step 1: Park idle connections to consume epoll watches
    print(f"[*] Parking {args.idle_conns} idle connections to consume epoll watches...")
    success = 0
    for i in range(args.idle_conns):
        if open_idle_connection(args.host, args.port):
            success += 1
        if i % 50 == 49:
            print(f"    {i+1}/{args.idle_conns} parked ({success} successful)")

    print(f"[*] {success} idle connections parked.")
    print()

    # Step 2: Fire CGI requests — each creates 2 new epoll watches (result + stdin pipe)
    print(f"[*] Firing {args.cgi_rounds} CGI requests to exhaust epoll budget...")
    for i in range(args.cgi_rounds):
        resp = send_cgi_request(args.host, args.port)
        status = "OK" if b"200" in resp or b"HTTP" in resp else "ERR/empty"
        print(f"    CGI request {i+1:3d}: {status}")
        if not check_server_alive(args.host, args.port):
            print("\n[!] Server stopped responding — possible crash from leaked state")
            break
        time.sleep(0.05)

    print()
    print("[*] Done. Now Ctrl+C the server and check Valgrind output.")
    print('    Look for: "definitely lost" blocks referencing _registerEventFd')
    print()

    # Close idle connections
    print(f"[*] Closing {len(IDLE_CONNECTIONS)} idle connections...")
    for s in IDLE_CONNECTIONS:
        try:
            s.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
