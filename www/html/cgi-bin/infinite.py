#!/usr/bin/env python3
import time
print("Status: 200 OK")
print("Content-Type: text/html")
print()
print("<h1>Infinite Loop Started</h1>")
print("<p>This will run forever...</p>")
import sys
sys.stdout.flush()
while True:
    time.sleep(1)
