#!/usr/bin/env python3
import time
import sys

# Sleep longer than the typical CGI timeout (e.g., 65 seconds for a 60‑second timeout).
# Adjust the number if your server uses a different timeout.
sleep_duration = 65

# Uncomment the next line to output headers before sleeping – this still triggers
# the timeout because the server waits for the *complete* response (child exit/EOF).
# print("Content-Type: text/plain\n")

# Sleep – the server will time out while waiting for the rest of the output.
time.sleep(sleep_duration)

# This will never be sent because the server kills the process before it finishes.
print("Content-Type: text/plain")
print()
print("This output should never reach the client.")