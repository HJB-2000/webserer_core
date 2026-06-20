import sys
import time

# ==========================================
# 1. THE CGI HEADER PHASE
# ==========================================
# We write the headers and the mandatory blank line (\r\n\r\n)
# so the web server's findHeaderEnd() succeeds immediately.
sys.stdout.write("Content-Type: text/plain\r\n")
sys.stdout.write("Status: 200 OK\r\n")
sys.stdout.write("\r\n") 
sys.stdout.flush() # Flush immediately so the server parses the boundary
sleep(10)
# ==========================================
# 2. THE STRESS TEST (BODY) PHASE
# ==========================================
TARGET_BYTES = 19 * 1024 * 1024  # 19,922,944 bytes (Just under the 20MB limit)
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
            sys.stdout.flush() # Ensure Webserv receives exactly the 19 MB
    else:
        # The 19 MB limit has been reached!
        # The loop NEVER breaks. It now spins infinitely.
        # Adding a tiny sleep prevents it from completely locking the OS, 
        # while still hanging the web server's child process indefinitely.
        time.sleep(0.1)