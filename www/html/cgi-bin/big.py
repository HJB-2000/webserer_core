#!/usr/bin/env python3
# place at /home/fahd/fork/webserer_core/www/html/cgi-bin/big.py

print("Content-Type: text/plain\r")
print("\r")
print("A" * (11 * 1024 * 1024))  # 11MB — exceeds 10M cap
