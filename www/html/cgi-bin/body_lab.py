#!/usr/bin/env python3
import os
import sys
import html

cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
body = sys.stdin.read(cl) if cl > 0 else "No body"
safe_body = html.escape(body[:1000], quote=True)

print("Status: 200 OK")
print("Content-Type: text/html")
print()
print("<html><body>")
print(f"<h1>Body Received</h1>")
print(f"<p>Content-Length: {cl} bytes</p>")
print(f"<pre>{safe_body}</pre>")
print("</body></html>")
