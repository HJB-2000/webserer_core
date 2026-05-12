#!/usr/bin/env python3
# Intentionally invalid CGI output for error-path testing: this line is not a
# valid header field (no colon), so the server should reject the script output.
print("this line is not a valid header because there is no colon separator")
print()
print("body")
