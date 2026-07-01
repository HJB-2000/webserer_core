#!/usr/bin/env python3
import os
import sys
import time
import urllib.parse

query_string = os.environ.get('QUERY_STRING', '')
params = urllib.parse.parse_qs(query_string)
seconds = int(params.get('seconds', ['15'])[0])

print("Status: 200 OK")
print("Content-Type: text/html")
print()

time.sleep(seconds)

print("<html><body>")
print(f"<h1>Sleep Test Complete: {seconds} seconds</h1>")
print("</body></html>")
