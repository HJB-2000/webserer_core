#!/usr/bin/env python3
import sys
import os

print("Content-Type: text/plain")
print("Status: 200 OK")
print()

print("=== CGI Test Script ===")
print(f"Server time: {__import__('datetime').datetime.now()}")
print(f"Request Method: {os.environ.get('REQUEST_METHOD')}")
print(f"Query String: {os.environ.get('QUERY_STRING')}")
print(f"PATH_INFO: {os.environ.get('PATH_INFO')}")
print(f"SCRIPT_NAME: {os.environ.get('SCRIPT_NAME')}")
print(f"All ENV vars: {list(os.environ.keys())}")
print("\n--- Script executed successfully! ---")