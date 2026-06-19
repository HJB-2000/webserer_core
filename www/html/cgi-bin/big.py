#!/usr/bin/env python3
import sys
import time

TOTAL_BYTES = 100 * 1024 * 1024   # 100MB, matches your existing stress profile
CHUNK_SIZE  = 64 * 1024
DELAY       = 0                   # seconds between writes; bump >0 to slow it down

sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("X-Total-Bytes: %d\r\n" % TOTAL_BYTES)
sys.stdout.write("\r\n")
sys.stdout.flush()

chunk = b"A" * CHUNK_SIZE
written = 0
out = sys.stdout.buffer

while written < TOTAL_BYTES:
    remaining = TOTAL_BYTES - written
    n = CHUNK_SIZE if remaining >= CHUNK_SIZE else remaining
    out.write(chunk[:n])
    out.flush()
    written += n
    if DELAY:
        time.sleep(DELAY)