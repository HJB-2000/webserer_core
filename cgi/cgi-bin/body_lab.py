#!/usr/bin/python3
import os
import sys
from html import escape

method = os.environ.get("REQUEST_METHOD", "")
content_type = os.environ.get("CONTENT_TYPE", "")
content_length = os.environ.get("CONTENT_LENGTH", "0")
raw_body = sys.stdin.read()

try:
    body_len = len(raw_body.encode("utf-8"))
except Exception:
    body_len = len(raw_body)

preview = raw_body[:500]

print("Content-Type: text/html")
print("Status: 200 OK")
print("X-CGI-Debug: body-lab")
print()
print("<!doctype html>")
print("<html><head><meta charset='utf-8'><title>Body Lab</title></head>")
print("<body style='font-family:monospace;background:#111827;color:#f9fafb;padding:24px;'>")
print("<h1>🧪 Body Lab</h1>")
print("<p>This endpoint helps test POST handling and stdin forwarding.</p>")
print("<ul>")
print("<li>Method: <b>{}</b></li>".format(escape(method)))
print("<li>CONTENT_TYPE: <b>{}</b></li>".format(escape(content_type)))
print("<li>CONTENT_LENGTH(env): <b>{}</b></li>".format(escape(content_length)))
print("<li>Body bytes received: <b>{}</b></li>".format(body_len))
print("</ul>")
print("<h3>Body preview (first 500 chars)</h3>")
print("<pre>{}</pre>".format(escape(preview if preview else "<empty body>")))
print("</body></html>")
