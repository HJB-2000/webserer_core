#!/usr/bin/env python3
import sys
import os
import html

content_length = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
body = sys.stdin.read(content_length) if content_length > 0 else "No body"
safe_body = html.escape(body[:1000], quote=True)

print("Status: 200 OK")
print("Content-Type: text/html\n")
print(f"<h1>Echo</h1>")
print(f"<p>Content-Length: {content_length} bytes</p>")
print(f"<pre>{safe_body}</pre>")
