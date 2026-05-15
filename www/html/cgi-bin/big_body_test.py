#!/usr/bin/env python3  
import sys  
  
print("Content-Type: text/plain\r")  
print("\r")  
# Output 11MB - exceeds server-level 10M but within location-level 30M/1G  
print("A" * (11 * 1024 * 1024))
