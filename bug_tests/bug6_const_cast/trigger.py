#!/usr/bin/env python3
"""
BUG 6 — const_cast Mutation of const-Reference Parameter
=========================================================
Sends path-traversal requests that exercise the const_cast mutation in
ResponseHandler::handle().

Tests performed:
  1. Baseline — normal path, confirm server responds correctly.
  2. Path traversal — send /../ path, observe what the server logs vs what
     was requested (the mutation is visible in stderr logs).
  3. UBSan check — if the server was compiled with -fsanitize=undefined,
     the const_cast line will print a runtime error to stderr.

Usage:
    # Option A: plain test (confirms behaviour)
    python3 trigger.py [--host 127.0.0.1] [--port 5000]

    # Option B: with UBSan to catch the cast at runtime
    make re CXXFLAGS="-g3 -std=c++98 -Wall -Wextra -fsanitize=undefined"
    ./webserv conf/confs/replit.conf 2>ubsan.log &
    python3 trigger.py
    cat ubsan.log   # look for "store to address which is const"
"""

import socket
import argparse
import sys

TEST_CASES = [
    ("/index.html",                "baseline — normal path"),
    ("/../index.html",             "one-level traversal"),
    ("/../../etc/passwd",          "two-level traversal"),
    ("/%2e%2e/index.html",         "percent-encoded traversal"),
    ("//double//slash",            "double slash normalisation"),
    ("/../../../../../etc/shadow", "deep traversal — sanitiser must strip all"),
]

def send_request(host, port, path):
    """Send a raw GET and return (status_line, raw_response)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
    except ConnectionRefusedError:
        return None, None

    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    )
    s.sendall(req.encode())

    resp = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            resp += chunk
    except socket.timeout:
        pass
    s.close()

    decoded = resp.decode(errors="replace")
    status  = decoded.split("\r\n", 1)[0] if decoded else "(empty)"
    return status, decoded

def main():
    parser = argparse.ArgumentParser(description="BUG 6 trigger — const_cast mutation")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 6 — const_cast Mutation of const-Reference")
    print("=" * 60)
    print(f"Target : {args.host}:{args.port}")
    print()
    print("NOTE: Run the server with stderr redirected to a file to see")
    print("      which sanitised path the server actually uses internally:")
    print("      ./webserv conf/confs/replit.conf 2>server.log")
    print()

    # Quick alive check
    status, _ = send_request(args.host, args.port, "/index.html")
    if status is None:
        print("[!] Server not responding. Start it first.")
        print("    make re && ./webserv conf/confs/replit.conf")
        sys.exit(1)

    print(f"{'PATH':<40}  {'DESCRIPTION':<35}  {'STATUS'}")
    print("-" * 100)

    for path, description in TEST_CASES:
        status, resp = send_request(args.host, args.port, path)
        if status is None:
            status = "(connection refused)"

        # Highlight suspicious cases
        flag = ""
        if "200" in status and ("passwd" in path or "shadow" in path):
            flag = "  ← SECURITY ISSUE"
        elif status == "(connection refused)":
            flag = "  ← server down?"

        print(f"  {path:<40}  {description:<35}  {status}{flag}")

    print()
    print("=" * 60)
    print("WHAT TO CHECK IN server.log / stderr")
    print("=" * 60)
    print()
    print("After a traversal request, grep the server log for the path it tried")
    print("to open on disk:")
    print()
    print("  grep '\\[ResponseHandler\\]\\|open\\|stat\\|fs_path' server.log")
    print()
    print("If the log shows the SANITISED path (e.g. '/index.html') rather than")
    print("the ORIGINAL path (e.g. '/../index.html'), the const_cast mutation")
    print("has changed conn->request().path in place — bug confirmed.")
    print()
    print("To catch it with UBSan at the point of mutation:")
    print("  make re CXXFLAGS=\"-g3 -std=c++98 -fsanitize=undefined\"")
    print("  ./webserv conf/confs/replit.conf 2>&1 | grep 'runtime error'")

if __name__ == "__main__":
    main()
