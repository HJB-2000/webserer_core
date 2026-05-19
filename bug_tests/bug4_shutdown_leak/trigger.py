#!/usr/bin/env python3
"""
BUG 4 — EventRef / CgiJob Leak on Shutdown
============================================
Creates several client connections and makes CGI requests so that
EventRef and CgiJob objects are allocated on the heap.
Then signals the server to shut down.
Valgrind will report "definitely lost" blocks from _registerEventFd/_startCgi.

Usage (see README.md for full instructions):
    # Terminal 1:
    valgrind --leak-check=full --track-origins=yes ./webserv conf/confs/replit.conf

    # Terminal 2:
    python3 trigger.py

    # Terminal 1: Ctrl+C — valgrind report appears.
"""

import socket
import time
import argparse
import sys
import threading

OPEN_SOCKETS = []

def open_client(host, port):
    """Open a connection, send a plain GET, keep socket open (don't read)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    try:
        s.connect((host, port))
        req = (
            f"GET /index.html HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            f"Connection: keep-alive\r\n"
            f"\r\n"
        )
        s.sendall(req.encode())
        OPEN_SOCKETS.append(s)
        return True
    except Exception:
        s.close()
        return False

def send_cgi(host, port):
    """Send a CGI request and collect the response (creates CgiJob + EventRef)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5)
    try:
        s.connect((host, port))
        req = (
            f"GET /cgi-bin/check_session.py HTTP/1.1\r\n"
            f"Host: {host}\r\n"
            f"Connection: close\r\n"
            f"\r\n"
        )
        s.sendall(req.encode())
        resp = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            resp += chunk
        s.close()
        return b"HTTP" in resp
    except Exception:
        s.close()
        return False

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
    parser = argparse.ArgumentParser(description="BUG 4 — leak on shutdown")
    parser.add_argument("--host",         default="127.0.0.1")
    parser.add_argument("--port",         type=int, default=5000)
    parser.add_argument("--client-count", type=int, default=10,
                        help="Number of open idle client connections (default 10)")
    parser.add_argument("--cgi-count",    type=int, default=5,
                        help="Number of completed CGI requests (default 5)")
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 4 — EventRef / CgiJob Leak on Shutdown")
    print("=" * 60)
    print(f"Target  : {args.host}:{args.port}")
    print()
    print("IMPORTANT: Server must be running under Valgrind.")
    print("           See README.md for instructions.")
    print()

    if not check_server_alive(args.host, args.port):
        print("[!] Server not responding. Start it first.")
        sys.exit(1)

    # Open idle connections — these create EventRef objects that leak on shutdown
    print(f"[*] Opening {args.client_count} idle client connections...")
    for i in range(args.client_count):
        ok = open_client(args.host, args.port)
        print(f"    conn {i+1:2d}: {'OK' if ok else 'FAIL'}")
        time.sleep(0.05)

    # Make CGI requests — these create CgiJob + extra EventRef objects
    print(f"\n[*] Sending {args.cgi_count} CGI requests...")
    for i in range(args.cgi_count):
        ok = send_cgi(args.host, args.port)
        print(f"    cgi  {i+1:2d}: {'response received' if ok else 'failed/timeout'}")
        time.sleep(0.1)

    print()
    print(f"[*] {len(OPEN_SOCKETS)} idle connections are still open (EventRefs allocated).")
    print()
    print(">>> NOW send Ctrl+C (SIGINT) to the server process.")
    print(">>> Valgrind will print its leak report as the server exits.")
    print()
    print("[*] Keeping connections open for 30s — send SIGINT to server now...")

    try:
        time.sleep(30)
    except KeyboardInterrupt:
        pass

    print("[*] Closing test connections.")
    for s in OPEN_SOCKETS:
        try:
            s.close()
        except Exception:
            pass

if __name__ == "__main__":
    main()
