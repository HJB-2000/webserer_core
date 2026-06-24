#!/usr/bin/env python3
import sys

sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("\r\n")
sys.stdout.flush()
raise Exception("Error!")
