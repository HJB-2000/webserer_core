#!/usr/bin/env python3
import os

print("Content-Type: text/html\r\n\r\n")
print("<!DOCTYPE html><html><head><title>CGI Hello</title></head>")
print("<body style='font-family:sans-serif;background:#0f172a;color:#e2e8f0;padding:30px;'>")
print(f"<h1>🐍 CGI Script Executed!</h1>")
print(f"<p>Server: {os.environ.get('SERVER_SOFTWARE', 'Unknown')}</p>")
print(f"<p>Method: {os.environ.get('REQUEST_METHOD', 'Unknown')}</p>")
print(f"<p>Query: {os.environ.get('QUERY_STRING', 'None')}</p>")
print("<a href='/' style='color:#38bdf8'>← Back</a></body></html>")