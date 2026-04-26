#!/usr/bin/env python3
import sys
import os

def main():
    # Read request body if available (simulate POST handling)
    content_length = int(os.environ.get('CONTENT_LENGTH', '0'))
    body = sys.stdin.read(content_length) if content_length > 0 else ''

    # Prepare CGI output
    status = "200 OK"
    # You can change this to test custom status parsing:
    # status = "201 Created"
    # status = "418 I'm a teapot"

    print(f"Status: {status}\r")
    print("Content-Type: text/plain\r")
    print("X-Custom-Header: test-value\r")
    print("\r")
    print("CGI script executed successfully!")
    print(f"Method: {os.environ.get('REQUEST_METHOD', 'UNKNOWN')}")
    print(f"Query: {os.environ.get('QUERY_STRING', '')}")
    if body:
        print(f"Body received ({len(body)} bytes): {body[:100]}")
    else:
        print("No request body.")

if __name__ == "__main__":
    main()