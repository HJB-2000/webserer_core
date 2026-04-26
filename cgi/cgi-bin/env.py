#!/usr/bin/env python3
import os

print("Content-Type: text/plain\r\n\r\n")
for key, value in sorted(os.environ.items()):
    print(f"{key}={value}")