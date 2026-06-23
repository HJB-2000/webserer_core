#!/usr/bin/env python3

import sys
import time

# Headers
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("\r\n")
sys.stdout.flush()

TARGET_BYTES = 19 * 1024 * 1024  # 19 MiB
CHUNK_SIZE = 1024
DATA_CHUNK = "A" * CHUNK_SIZE

bytes_written = 0

while bytes_written < TARGET_BYTES:
    remaining = TARGET_BYTES - bytes_written

    if remaining >= CHUNK_SIZE:
        sys.stdout.write(DATA_CHUNK)
        bytes_written += CHUNK_SIZE
    else:
        sys.stdout.write("A" * remaining)
        bytes_written += remaining

sys.stdout.flush()

while True:
    time.sleep(0.1)