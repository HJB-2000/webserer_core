import sys
import time

TARGET_BYTES = 19 * 1024 * 1024  # 19,922,944 bytes
CHUNK_SIZE = 1024
DATA_CHUNK = "A" * CHUNK_SIZE

bytes_written = 0

while True:
    if bytes_written < TARGET_BYTES:
        # Check if a full chunk fits, otherwise write the exact remainder
        if bytes_written + CHUNK_SIZE <= TARGET_BYTES:
            sys.stdout.write(DATA_CHUNK)
            bytes_written += CHUNK_SIZE
        else:
            remaining_bytes = TARGET_BYTES - bytes_written
            sys.stdout.write("A" * remaining_bytes)
            bytes_written += remaining_bytes
            sys.stdout.flush() # Ensure Apache receives exactly the 19 MB
    else:
        # The 19 MB limit has been reached!
        # The loop NEVER breaks. It now spins infinitely, exhausting the CPU.
        # We add a tiny sleep or pass to simulate an aggressive infinite loop.
        pass
