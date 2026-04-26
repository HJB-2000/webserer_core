#!/usr/bin/env python3
import os
import sys
import urllib.parse

def log_error(msg):
    sys.stderr.write(f"[delete_user] {msg}\n")
    sys.stderr.flush()

# Read request method
request_method = os.environ.get('REQUEST_METHOD', '')
log_error(f"REQUEST_METHOD = {request_method}")

# Read content length
content_length_str = os.environ.get('CONTENT_LENGTH', '0')
try:
    content_length = int(content_length_str)
except ValueError:
    content_length = 0

log_error(f"CONTENT_LENGTH = {content_length}")

# Read the body
raw_body = b''
if content_length > 0:
    raw_body = sys.stdin.buffer.read(content_length)

log_error(f"Raw body (repr): {repr(raw_body)}")

# Try to decode and parse
body_str = raw_body.decode('utf-8', errors='replace')
params = urllib.parse.parse_qs(body_str)

username = params.get('username', [''])[0]
timestamp = params.get('timestamp', [''])[0]

log_error(f"username = '{username}', timestamp = '{timestamp}'")

# For debugging, ALWAYS return a 200 with the received data (temporarily)
print("Content-Type: text/plain\r\n")
print(f"Method: {request_method}")
print(f"Content-Length header: {content_length_str}")
print(f"Raw body (hex): {raw_body.hex()}")
print(f"Decoded body string: {body_str}")
print(f"Parsed username: '{username}'")
print(f"Parsed timestamp: '{timestamp}'")

# Then, if both are non-empty, perform deletion
if username and timestamp:
    log_file = "/home/fahd/webserv/www/html/user_log.txt"
    if os.path.exists(log_file):
        with open(log_file, "r") as f:
            lines = f.readlines()
        target_line = f"[{timestamp}] LOGIN SUCCESS: {username}\n"
        new_lines = [line for line in lines if line.strip() != target_line.strip()]
        if len(new_lines) != len(lines):
            with open(log_file, "w") as f:
                f.writelines(new_lines)
            print("\nAction: User deleted successfully.")
        else:
            print("\nAction: User not found.")
    else:
        print("\nAction: Log file not found.")
else:
    print("\nAction: Missing username or timestamp – no deletion attempted.")