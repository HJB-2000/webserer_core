#!/usr/bin/env python3
"""
BUG 7 — Chunked Body Trailing CRLF Not Validated
=================================================
Sends chunked POST requests where the 2-byte trailing separator after
each chunk's data is NOT \\r\\n, testing whether the server accepts them.

A correct RFC 7230-compliant server must return 400 Bad Request.
A buggy server (with the unvalidated consume(2)) returns 2xx.

Tests:
  Case 1: trailing \\r\\x00   (CR + NUL)
  Case 2: trailing \\n\\n     (two LFs)
  Case 3: trailing \\x00\\x00 (two NUL bytes)
  Case 4: trailing AB         (printable garbage)
  Case 5: trailing \\r\\n     (correct — baseline, must be 2xx)

Usage:
    python3 trigger.py [--host 127.0.0.1] [--port 5000] [--path /uploads/]
"""

import socket
import argparse
import sys
import time

def build_chunked_post(host, path, chunk_data, bad_trailing):
    """
    Build a chunked POST request where the trailing CRLF after the chunk
    data is replaced with `bad_trailing` (bytes).

    Wire format produced:
        POST /path HTTP/1.1\\r\\n
        Host: host\\r\\n
        Transfer-Encoding: chunked\\r\\n
        Connection: close\\r\\n
        \\r\\n
        <hex-len>\\r\\n
        <chunk_data><bad_trailing>   ← bad trailing here
        0\\r\\n
        \\r\\n
    """
    hex_len    = format(len(chunk_data), 'x').encode()
    chunk_body = hex_len + b"\r\n" + chunk_data + bad_trailing
    term       = b"0\r\n\r\n"

    headers = (
        f"POST {path} HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Transfer-Encoding: chunked\r\n"
        f"Connection: close\r\n"
        f"\r\n"
    ).encode()

    return headers + chunk_body + term

def send_raw(host, port, data, timeout=5.0):
    """Send raw bytes, return full response as string."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
    except ConnectionRefusedError:
        return None

    s.sendall(data)

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

def status_of(response):
    """Extract HTTP status line from response."""
    if not response:
        return "(no response)"
    return response.split("\r\n", 1)[0]

def is_success(status_line):
    """Return True if status is 2xx."""
    parts = status_line.split()
    if len(parts) >= 2:
        try:
            code = int(parts[1])
            return 200 <= code < 300
        except ValueError:
            pass
    return False

def is_bad_request(status_line):
    return "400" in status_line

def main():
    parser = argparse.ArgumentParser(description="BUG 7 — chunked CRLF validation")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--path", default="/uploads/",
                        help="POST path (must allow POST; default /uploads/)")
    args = parser.parse_args()

    print("=" * 65)
    print("BUG 7 — Chunked Trailing CRLF Not Validated")
    print("=" * 65)
    print(f"Target : {args.host}:{args.port}")
    print(f"Path   : {args.path}")
    print()
    print("A CORRECT server returns 400 Bad Request for all malformed cases.")
    print("A BUGGY  server returns 2xx for some or all malformed cases.")
    print()

    # Alive check
    alive = send_raw(args.host, args.port,
                     f"GET / HTTP/1.1\r\nHost: {args.host}\r\nConnection: close\r\n\r\n".encode())
    if alive is None:
        print("[!] Server not responding. Start it first:")
        print("    make re && ./webserv conf/confs/replit.conf")
        sys.exit(1)

    chunk_data = b"hello"

    test_cases = [
        (b"\r\n",     "\\r\\n (CORRECT — baseline)",        True),
        (b"\r\x00",   "\\r\\x00 (CR + NUL) — MALFORMED",   False),
        (b"\n\n",     "\\n\\n (two LFs) — MALFORMED",       False),
        (b"\x00\x00", "\\x00\\x00 (two NUL) — MALFORMED",  False),
        (b"AB",       "AB (printable garbage) — MALFORMED", False),
        (b"\r\r",     "\\r\\r (two CRs) — MALFORMED",       False),
    ]

    bugs_found = 0

    print(f"  {'TRAILING':<28}  {'EXPECT':<12}  {'GOT':<25}  RESULT")
    print("  " + "-" * 90)

    for trailing_bytes, label, expect_success in test_cases:
        req  = build_chunked_post(args.host, args.path, chunk_data, trailing_bytes)
        resp = send_raw(args.host, args.port, req)
        status = status_of(resp)
        got_success = is_success(status)

        expected_str = "2xx (OK)"   if expect_success else "400 Bad Req"

        if expect_success:
            result = "PASS" if got_success else "WARN (baseline rejected)"
        else:
            if got_success:
                result = "BUG CONFIRMED — server accepted malformed request"
                bugs_found += 1
            else:
                result = "PASS"

        print(f"  {label:<28}  {expected_str:<12}  {status:<25}  {result}")
        time.sleep(0.05)

    print()
    print("=" * 65)
    if bugs_found > 0:
        print(f"[BUG CONFIRMED] {bugs_found} malformed chunked request(s) accepted as valid.")
        print()
        print("Fix in src/HttpParser.cpp, _parseChunked(), step 1:")
        print("  if (buf.data()[0] != '\\r' || buf.data()[1] != '\\n') {")
        print("      req.parse_state = PSTATE_ERROR;")
        print("      req.error_code  = 400;")
        print("      return;")
        print("  }")
    else:
        print("[PASS] All malformed requests correctly rejected with 400.")
        print("       Bug 7 may already be fixed, or the upload path returned")
        print("       an error for a different reason.")
        print("       Try --path with a location that allows POST.")
    print("=" * 65)

if __name__ == "__main__":
    main()
