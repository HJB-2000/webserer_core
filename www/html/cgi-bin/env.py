#!/usr/bin/env python3
import os
print("Status: 200 OK")
print("Content-Type: text/html\n")
print("<h1>CGI Environment Variables</h1><pre>")
for key, value in sorted(os.environ.items()):
    print(f"{key}: {value}")
print("</pre>")
