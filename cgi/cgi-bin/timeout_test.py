#!/usr/bin/env python3
import time

# Sleep for 60 seconds before sending any output.
# This will exceed most CGI timeouts (typically 5–30 seconds).
time.sleep(60)

# If the server waits this long, it will eventually get this response.
print("Content-Type: text/plain")
print("Status: 200 OK")
print()
print("This should not appear if the server times out.")
