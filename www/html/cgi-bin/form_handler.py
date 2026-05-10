#!/usr/bin/env python3
import os
import urllib.parse
import sys

method = os.environ.get('REQUEST_METHOD', 'GET')

print("Status: 200 OK")
print("Content-Type: text/html\n")
print("<h1>Form Handler</h1>")

if method == 'GET':
    query = os.environ.get('QUERY_STRING', '')
    params = urllib.parse.parse_qs(query)
    print(f"<h2>GET Parameters:</h2><pre>{params}</pre>")
elif method == 'POST':
    cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
    body = sys.stdin.read(cl) if cl > 0 else ""
    params = urllib.parse.parse_qs(body)
    print(f"<h2>POST Parameters:</h2><pre>{params}</pre>")
else:
    print(f"<h2>Method: {method}</h2>")
