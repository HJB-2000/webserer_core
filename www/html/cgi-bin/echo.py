#!/usr/bin/env python3
import sys
import os

content_length = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
body = sys.stdin.read(content_length) if content_length > 0 else "No body"

print("Status: 200 OK")
print("Content-Type: text/html\n")
print(f"<h1>Echo</h1>")
print(f"<p>Content-Length: {content_length} bytes</p>")
print(f"<pre>{body[:1000]}</pre>")
