#!/usr/bin/env python3
import time
# Don't send headers immediately - this causes 408
time.sleep(15)
print("Status: 200 OK")
print("Content-Type: text/html\n")
print("<html><body>This should timeout first</body></html>")
