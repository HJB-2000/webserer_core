#!/usr/bin/env python3
# CGI env dump — Phase 4 test
import os

print("Content-Type: text/html\r")
print("\r")
print("<!DOCTYPE html>")
print("<html><head><title>CGI Env</title></head><body>")
print("<h1>CGI Environment</h1><pre>")
for k, v in sorted(os.environ.items()):
    print(f"{k} = {v}")
print("</pre></body></html>")
