#!/usr/bin/env python3
import os
import sys
import html
import resource

def get_memory_mb():
    """Get current process memory in MB using getrusage"""
    usage = resource.getrusage(resource.RUSAGE_SELF)
    # Maximum resident set size (in KB on Linux)
    return usage.ru_maxrss / 1024.0

cl = int(os.environ.get('CONTENT_LENGTH', '0') or '0')
print(f"[CGI-Child-Memory] Initial memory: {get_memory_mb():.2f} MB", file=sys.stderr)
print(f"[CGI-Child-Memory] Will read {cl} bytes from stdin", file=sys.stderr)

body = sys.stdin.read(cl) if cl > 0 else "No body"
print(f"[CGI-Child-Memory] After reading stdin, memory: {get_memory_mb():.2f} MB", file=sys.stderr)
print(f"[CGI-Child-Memory] Body length: {len(body)} bytes", file=sys.stderr)

safe_body = html.escape(body[:1000], quote=True)
print(f"[CGI-Child-Memory] After escaping body[:1000], memory: {get_memory_mb():.2f} MB", file=sys.stderr)

print("Status: 200 OK")
print("Content-Type: text/html")
print()
print("<html><body>")
print(f"<h1>Body Received</h1>")
print(f"<p>Content-Length: {cl} bytes</p>")
print(f"<pre>{safe_body}</pre>")
print("</body></html>")
