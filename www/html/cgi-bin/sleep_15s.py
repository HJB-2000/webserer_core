#!/usr/bin/env python3
import os
import sys
import time
import urllib.parse

# Don't send any output during the sleep
query_string = os.environ.get('QUERY_STRING', '')
params = urllib.parse.parse_qs(query_string)
seconds = int(params.get('seconds', ['40'])[0])

# Send headers immediately
print("Status: 200 OK")
print("Content-Type: text/html")
print()
# sys.stdout.flush()

# Now just sleep - don't send progress updates
# This simulates a slow CGI script that hangs
time.sleep(seconds)

# Only send the body AFTER the sleep (but this will never be seen if timeout happens)
print("<html><body>")
print(f"<h1>Sleep Test Complete: {seconds} seconds</h1>")
print("</body></html>")
