#!/usr/bin/env python3

import sys

# Proper CGI headers with CRLF (\r\n) line endings
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("\r\n")  # Blank line separates headers from body
sys.stdout.flush()

# Write 11MB of data — exceeds 10M cap to test BodyLimitException handling
sys.stdout.write("A" * (11 * 1024 * 1024))
sys.stdout.flush()