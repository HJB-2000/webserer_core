#!/usr/bin/env python3
"""
BUG 3 — EPOLLOUT Dropped on Simultaneous EPOLLIN + EPOLLOUT
=============================================================
Strategy:
  1. Create a large response payload (>= kernel socket send buffer, ~128KB+).
     We do this by generating a big file on disk if it doesn't exist.
  2. Open a keep-alive connection and request the large file WITHOUT reading
     the response — this fills the server's send buffer (EPOLLOUT will be armed).
  3. Immediately send a second pipelined request on the same socket
     (EPOLLIN arrives at the server).
  4. Now start reading. In edge-triggered mode, if both EPOLLIN and EPOLLOUT
     arrived together in one epoll_wait call, the server reads the second request
     (EPOLLIN handled) but never drains its write buffer (EPOLLOUT dropped).
  5. The second response never arrives → timeout → bug confirmed.

Usage:
    python3 trigger.py [--host 127.0.0.1] [--port 5000]
"""

import socket
import time
import argparse
import sys
import os

LARGE_FILE_PATH = "www/html/bug3_large.bin"
LARGE_FILE_SIZE = 2 * 1024 * 1024   # 2 MB — well above typical socket buffers

def ensure_large_file(root_dir):
    """Create the large test file if it doesn't exist."""
    path = os.path.join(root_dir, LARGE_FILE_PATH)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if not os.path.exists(path):
        print(f"[*] Creating large test file ({LARGE_FILE_SIZE // 1024} KB): {path}")
        with open(path, "wb") as f:
            # Repeating pattern — compresses badly so TCP truly sends all bytes
            chunk = b"X" * 4096
            written = 0
            while written < LARGE_FILE_SIZE:
                f.write(chunk)
                written += len(chunk)
        print(f"[*] Large file created.")
    else:
        size = os.path.getsize(path)
        print(f"[*] Large test file exists ({size // 1024} KB): {path}")
    return path

def make_get_request(path, host, keep_alive=True):
    conn_header = "keep-alive" if keep_alive else "close"
    return (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Connection: {conn_header}\r\n"
        f"\r\n"
    ).encode()

def read_response_header(sock, timeout=5.0):
    """Read until \\r\\n\\r\\n, return (headers_str, leftover_bytes)."""
    sock.settimeout(timeout)
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                return None, buf
            buf += chunk
    except socket.timeout:
        return None, buf
    idx = buf.index(b"\r\n\r\n")
    return buf[:idx + 4].decode(errors="replace"), buf[idx + 4:]

def parse_content_length(headers_str):
    for line in headers_str.splitlines():
        if line.lower().startswith("content-length:"):
            try:
                return int(line.split(":", 1)[1].strip())
            except ValueError:
                pass
    return None

def main():
    parser = argparse.ArgumentParser(description="BUG 3 trigger — EPOLLOUT dropped")
    parser.add_argument("--host",    default="127.0.0.1")
    parser.add_argument("--port",    type=int, default=5000)
    parser.add_argument("--timeout", type=float, default=8.0,
                        help="Seconds to wait for second response (default 8)")
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 3 — EPOLLOUT Dropped (simultaneous EPOLLIN+EPOLLOUT)")
    print("=" * 60)
    print(f"Target  : {args.host}:{args.port}")
    print(f"Timeout : {args.timeout}s")
    print()

    # Find the project root (two levels up from this script)
    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir   = os.path.abspath(os.path.join(script_dir, "..", ".."))

    ensure_large_file(root_dir)
    print()

    # Connect
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    try:
        s.connect((args.host, args.port))
    except ConnectionRefusedError:
        print("[!] Cannot connect — is the server running?")
        print("    make re && ./webserv conf/confs/replit.conf")
        sys.exit(1)

    print("[*] Connected to server.")

    # Step 1: Send request for the large file
    req1 = make_get_request("/bug3_large.bin", args.host, keep_alive=True)
    s.sendall(req1)
    print(f"[*] Sent request 1 (large file, {LARGE_FILE_SIZE // 1024} KB expected response).")

    # Step 2: Immediately send a second request WITHOUT reading the first response.
    # This is the key: we want data piled in the receive buffer at the server
    # while the server's send buffer is also full.
    req2 = make_get_request("/index.html", args.host, keep_alive=False)
    s.sendall(req2)
    print("[*] Sent request 2 (pipelined — index.html) WITHOUT reading response 1.")

    # Step 3: Now start reading. Drain the first response.
    print(f"[*] Reading response 1 (headers)...")
    headers1, leftover = read_response_header(s, timeout=args.timeout)
    if headers1 is None:
        print("[!] Timeout reading response 1 headers. Server might be broken already.")
        s.close()
        sys.exit(1)

    print(f"[*] Response 1 headers received.")
    cl1 = parse_content_length(headers1)
    if cl1 is not None:
        print(f"[*] Content-Length: {cl1}. Draining {cl1} bytes...")
        received = len(leftover)
        s.settimeout(args.timeout)
        try:
            while received < cl1:
                chunk = s.recv(65536)
                if not chunk:
                    break
                received += len(chunk)
        except socket.timeout:
            print("[!] Timeout draining response 1 body.")
        print(f"[*] Drained {received}/{cl1} bytes of response 1.")
    else:
        print("[*] No Content-Length — draining until separator...")
        # For chunked or connection:close, just drain leftover
        pass

    # Step 4: Now try to read response 2.
    # If EPOLLOUT was dropped, the server never flushed its write buffer after
    # processing request 2 → response 2 never arrives → timeout.
    print(f"[*] Waiting for response 2 (timeout={args.timeout}s)...")
    t_start = time.time()
    headers2, _ = read_response_header(s, timeout=args.timeout)
    elapsed = time.time() - t_start

    print()
    if headers2 is None:
        print("=" * 60)
        print("[STALL DETECTED] Response 2 never arrived after {:.1f}s.".format(elapsed))
        print("                 EPOLLOUT was dropped — BUG 3 confirmed.")
        print("=" * 60)
    else:
        status_line = headers2.splitlines()[0] if headers2 else "(empty)"
        print("=" * 60)
        print(f"[OK] Response 2 received in {elapsed:.2f}s: {status_line}")
        print("     Bug not triggered in this run.")
        print("     Try with more parallel connections or a larger file.")
        print("=" * 60)

    s.close()

if __name__ == "__main__":
    main()
