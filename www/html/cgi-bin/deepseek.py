#!/usr/bin/env python3
import sys
import os
import html

cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
body = sys.stdin.read(cl) if cl > 0 else ""

print("Status: 200 OK")
print("Content-Type: text/html\n")
print("<h1>DeepSeek Raw Body Fuzzer</h1>")
print(f"<p>Content-Length: {cl}</p>")
print(f"<p>Raw Body (hex dump):</p><pre>")
for i, byte in enumerate(body.encode('utf-8', 'replace')[:500]):
    line = f"{i:04x}: {byte:02x} {chr(byte) if 32 <= byte < 127 else '.'}"
    print(html.escape(line, quote=True))
print("</pre>")
