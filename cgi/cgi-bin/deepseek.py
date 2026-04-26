#!/usr/bin/env python3
"""
DeepSeek CGI script – reads raw stdin bytes (including nulls, unicode, garbage)
and returns a detailed hex dump + analysis.
"""

import sys
import os

def escape_bytes(data, max_len=1024):
    """Return a string where non-printable bytes are shown as \\xHH or \\0."""
    out = []
    for i, b in enumerate(data):
        if i >= max_len:
            out.append("...")
            break
        if 32 <= b < 127:
            out.append(chr(b))
        elif b == 0:
            out.append("\\0")
        else:
            out.append(f"\\x{b:02x}")
    return ''.join(out)

def main():
    # Read all raw bytes from stdin (CGI gives us the request body)
    raw_body = sys.stdin.buffer.read()
    body_len = len(raw_body)

    # Build HTML response
    print("Content-Type: text/html; charset=UTF-8")
    print("Status: 200 OK")
    # Optional: force download as binary (uncomment if you want to test raw output)
    # print("Content-Disposition: attachment; filename=\"raw.bin\"")
    print()  # blank line between headers and body

    print("""<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DeepSeek – Raw Body Inspector</title>
    <style>
        body { font-family: monospace; background: #0f172a; color: #e2e8f0; padding: 20px; }
        .container { max-width: 1200px; margin: auto; background: #1e293b; padding: 20px; border-radius: 10px; }
        h1 { color: #ec4899; }
        pre { background: #0f172a; padding: 15px; border-radius: 8px; overflow-x: auto; border-left: 4px solid #ec4899; white-space: pre-wrap; word-wrap: break-word; }
        table { width: 100%; border-collapse: collapse; margin: 15px 0; }
        th, td { border: 1px solid #334155; padding: 8px; text-align: left; }
        th { background: #334155; }
        .badge { display: inline-block; background: #ec4899; padding: 4px 12px; border-radius: 20px; font-size: 0.8em; }
    </style>
</head>
<body>
<div class="container">
    <h1>🌀 DeepSeek – Raw Body Inspection</h1>
    <p><span class="badge">Received via POST</span></p>
""")

    # Environment info (only show relevant CGI vars)
    print("<h2>Environment</h2>")
    print("<table>")
    for var in ['REQUEST_METHOD', 'CONTENT_TYPE', 'CONTENT_LENGTH', 'HTTP_USER_AGENT']:
        val = os.environ.get(var, '')
        print(f"<tr><th>{var}</th><td>{val}</td></tr>")
    print("</table>")

    # Raw body statistics
    print("<h2>Raw Body Statistics</h2>")
    print(f"<p><strong>Total bytes read from stdin:</strong> {body_len}</p>")

    # Hex dump (first 2048 bytes)
    max_display = 2048
    print(f"<h2>Hex Dump (first {min(body_len, max_display)} bytes)</h2>")
    print("<pre>")
    if body_len == 0:
        print("(empty body)")
    else:
        hex_lines = []
        for i in range(0, min(body_len, max_display), 16):
            chunk = raw_body[i:i+16]
            hex_part = ' '.join(f"{b:02x}" for b in chunk)
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
            hex_lines.append(f"{i:08x}  {hex_part:<48}  {ascii_part}")
        print('\n'.join(hex_lines))
        if body_len > max_display:
            print(f"... and {body_len - max_display} more bytes")
    print("</pre>")

    # Raw bytes as Python repr (shows nulls as \x00)
    print("<h2>Python repr() of first 256 bytes</h2>")
    print("<pre>")
    if body_len > 0:
        sample = raw_body[:256]
        print(repr(sample))
    else:
        print("b''")
    print("</pre>")

    # NEW: Human-readable escaped version (nulls become \0)
    print("<h2>Escaped Raw Body (first 1024 bytes)</h2>")
    print("<pre>")
    if body_len > 0:
        escaped = escape_bytes(raw_body, 1024)
        print(escaped)
    else:
        print("(empty)")
    print("</pre>")

    # Attempt to decode as UTF-8 (if possible)
    print("<h2>UTF-8 Decoding Attempt</h2>")
    print("<pre>")
    try:
        decoded = raw_body.decode('utf-8')
        print(decoded[:2000] if len(decoded) > 2000 else decoded)
        if len(decoded) > 2000:
            print("\n... (truncated)")
    except UnicodeDecodeError as e:
        print(f"Decoding failed: {e}")
    print("</pre>")

    # Bonus: Check for null bytes explicitly
    if b'\x00' in raw_body:
        print("<p style='color:#facc15;'>⚠️ Null bytes (\\x00) detected in body.</p>")

    print("""
    <p><a href="/index.html" style="color:#ec4899;">← Back to WebServ Suite</a></p>
</div>
</body>
</html>
""")

if __name__ == "__main__":
    main()