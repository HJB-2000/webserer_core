#!/usr/bin/env python3
import os
import sys
from urllib.parse import parse_qs
from html import escape

print("Content-Type: text/html\r\n\r\n")

method = os.environ.get("REQUEST_METHOD", "GET")
query = os.environ.get("QUERY_STRING", "")
body = ""

if method == "POST":
    body = sys.stdin.read()

params = parse_qs(query if method == "GET" else body, keep_blank_values=True)

print("<!DOCTYPE html><html><body style='font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:30px;'>")
print("<h1>📝 Form Data Received</h1><ul>")
if params:
    for field in sorted(params.keys()):
        value = ", ".join(params[field])
        print(f"<li><b>{escape(field)}</b>: {escape(value)}</li>")
else:
    print("<li><i>No form/query fields provided</i></li>")
print("</ul><a href='/' style='color:#38bdf8'>← Back</a></body></html>")
