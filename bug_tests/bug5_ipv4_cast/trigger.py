#!/usr/bin/env python3
"""
BUG 5 — addrToString IPv4-Only Cast Without Family Check
=========================================================
Tests whether the server correctly reports REMOTE_ADDR in the CGI environment.

Two test modes:
  1. IPv4 connection (should always work — baseline)
  2. IPv6 loopback connection (triggers wrong ss_family if server accepts it)

The CGI script at /cgi-bin/check_session.py must print REMOTE_ADDR.
We extract it from the CGI output and compare against what we expect.

Usage:
    python3 trigger.py [--host 127.0.0.1] [--port 5000]

Options:
    --host      Server IPv4 address (default: 127.0.0.1)
    --port      Server port (default: 5000)
    --ipv6      Also test via IPv6 (::1) connection
"""

import socket
import argparse
import sys
import re

CGI_PATH = "/cgi-bin/check_session.py"

def raw_get(host, port, path, family=socket.AF_INET):
    """Send a raw GET request and return the full response body."""
    s = socket.socket(family, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
    except (ConnectionRefusedError, OSError) as e:
        print(f"[!] Connection failed: {e}")
        return None

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
    return resp.decode(errors="replace")

def extract_remote_addr(response):
    """Extract REMOTE_ADDR= from CGI output (plain text or HTML)."""
    if not response:
        return None
    # Look for REMOTE_ADDR=x.x.x.x in the body (after headers)
    body_start = response.find("\r\n\r\n")
    body = response[body_start + 4:] if body_start != -1 else response

    # Match REMOTE_ADDR=... (CGI env dump format)
    m = re.search(r"REMOTE_ADDR[=:\s]+([0-9a-fA-F.:]+)", body)
    if m:
        return m.group(1).strip()
    return None

def extract_status(response):
    if not response:
        return "no response"
    first_line = response.split("\r\n", 1)[0]
    return first_line

def main():
    parser = argparse.ArgumentParser(description="BUG 5 trigger — IPv4 cast")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--ipv6", action="store_true",
                        help="Also test via IPv6 (::1) connection")
    args = parser.parse_args()

    print("=" * 60)
    print("BUG 5 — addrToString IPv4-Only Cast")
    print("=" * 60)
    print(f"Target : {args.host}:{args.port}")
    print()

    # ── Test 1: IPv4 baseline ─────────────────────────────────
    print("[TEST 1] IPv4 connection (baseline — should always pass)")
    resp4 = raw_get(args.host, args.port, CGI_PATH, socket.AF_INET)
    if resp4 is None:
        print("[!] Server not responding. Start it first.")
        sys.exit(1)

    status4      = extract_status(resp4)
    remote_addr4 = extract_remote_addr(resp4)

    print(f"  HTTP status : {status4}")
    print(f"  REMOTE_ADDR : {remote_addr4!r}")
    print(f"  Expected    : '127.0.0.1'")

    if remote_addr4 == "127.0.0.1":
        print("  [PASS] REMOTE_ADDR is correct for IPv4.")
    elif remote_addr4 is None:
        print("  [WARN] REMOTE_ADDR not found in CGI output.")
        print("         Check that /cgi-bin/check_session.py prints REMOTE_ADDR.")
    else:
        print(f"  [FAIL] REMOTE_ADDR is WRONG: got {remote_addr4!r}, expected '127.0.0.1'")
    print()

    # ── Test 2: IPv6 loopback ─────────────────────────────────
    if args.ipv6:
        print("[TEST 2] IPv6 loopback connection (::1) — triggers wrong ss_family")
        try:
            resp6 = raw_get("::1", args.port, CGI_PATH, socket.AF_INET6)
        except OSError as e:
            print(f"  [SKIP] IPv6 not available: {e}")
            resp6 = None

        if resp6:
            status6      = extract_status(resp6)
            remote_addr6 = extract_remote_addr(resp6)
            print(f"  HTTP status : {status6}")
            print(f"  REMOTE_ADDR : {remote_addr6!r}")
            print(f"  Expected    : '::1' or '0:0:0:0:0:0:0:1'")

            if remote_addr6 and ("::1" in remote_addr6 or "0:0:0:0:0:0:0:1" in remote_addr6):
                print("  [PASS] REMOTE_ADDR is correct for IPv6.")
            elif remote_addr6 is None:
                print("  [WARN] REMOTE_ADDR not found in CGI output.")
            else:
                print(f"  [BUG CONFIRMED] Wrong REMOTE_ADDR for IPv6 connection!")
                print(f"                  Got {remote_addr6!r} instead of '::1'")
                print(f"                  addrToString cast IPv6 sockaddr as IPv4.")
        print()

    # ── Test 3: Check CGI REMOTE_ADDR via multiple connections ──
    print("[TEST 3] Confirm REMOTE_ADDR consistency across 5 connections")
    results = []
    for i in range(5):
        r = raw_get(args.host, args.port, CGI_PATH, socket.AF_INET)
        addr = extract_remote_addr(r)
        results.append(addr)

    unique = set(results)
    print(f"  Results : {results}")
    if unique == {"127.0.0.1"}:
        print("  [PASS] Consistent correct REMOTE_ADDR across all connections.")
    elif None in unique:
        print("  [WARN] Some responses missing REMOTE_ADDR field.")
    else:
        print(f"  [FAIL] Inconsistent REMOTE_ADDR values: {unique}")

    print()
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    if not args.ipv6:
        print("Run with --ipv6 to test the IPv6 path that triggers the cast bug.")
        print("The bug is most clearly visible when the kernel dual-stacks the socket.")

if __name__ == "__main__":
    main()
