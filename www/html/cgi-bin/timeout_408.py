#!/usr/bin/env python3
import time
# Don't send any data for a long time
time.sleep(15)
print("Status: 200 OK")
print("Content-Type: text/plain")
print()
print("Slept 15 seconds before responding.")
