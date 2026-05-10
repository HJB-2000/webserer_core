#!/usr/bin/env python3
import os
import sys

cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
body = sys.stdin.read(cl) if cl > 0 else "No body"

print("Status: 200 OK")
print("Content-Type: text/html")
print()
print("<html><body>")
print(f"<h1>Body Received</h1>")
print(f"<p>Content-Length: {cl} bytes</p>")
print(f"<pre>{body[:1000]}</pre>")
print("</body></html>")
