#!/usr/bin/env python3
"""
Heavy CGI tester - simulates 20 concurrent clients sending 100MB POST requests.
Based on the original tester behavior observed in logs.
"""
import subprocess
import time
import os
import sys
import signal
from multiprocessing import Process, Queue

# Configuration
HOST = "127.0.0.1"
PORT = 8080
NUM_CLIENTS = 20
BODY_SIZE = 100 * 1024 * 1024  # 100MB
ENDPOINT = "/cgi-bin/body_lab.py"
NUM_RUNS = 1  # Number of times to run the test

def make_request(client_id, body_size, results_queue):
    """Send a single POST request with large body"""
    import socket
    import random
    
    # Generate random body data
    body = os.urandom(body_size)
    
    request = (
        f"POST {ENDPOINT} HTTP/1.1\r\n"
        f"Host: {HOST}:{PORT}\r\n"
        f"Content-Type: application/octet-stream\r\n"
        f"Content-Length: {body_size}\r\n"
        f"Connection: close\r\n"
        f"X-Client-ID: {client_id}\r\n"
        f"\r\n"
    ).encode()
    
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(60)
        s.connect((HOST, PORT))
        
        # Send headers
        s.sendall(request)
        
        # Send body in chunks
        chunk_size = 1024 * 1024  # 1MB chunks
        sent = 0
        while sent < body_size:
            to_send = min(chunk_size, body_size - sent)
            s.sendall(body[sent:sent + to_send])
            sent += to_send
        
        # Read response
        response = b""
        while True:
            try:
                chunk = s.recv(65536)
                if not chunk:
                    break
                response += chunk
                # Check if we got the complete response
                if b"</html>" in response or b"</body>" in response:
                    break
            except socket.timeout:
                break
        
        s.close()
        results_queue.put((client_id, "OK", len(response)))
    except Exception as e:
        results_queue.put((client_id, f"ERROR: {e}", 0))

def run_test():
    """Run the heavy test with N concurrent clients"""
    print(f"[Tester] Starting test with {NUM_CLIENTS} clients, {BODY_SIZE} bytes each")
    print(f"[Tester] Target: http://{HOST}:{PORT}{ENDPOINT}")
    
    results_queue = Queue()
    processes = []
    start_time = time.time()
    
    # Start all clients concurrently
    for i in range(NUM_CLIENTS):
        p = Process(target=make_request, args=(i, BODY_SIZE, results_queue))
        p.start()
        processes.append(p)
    
    # Wait for all to complete
    for p in processes:
        p.join(timeout=120)
        if p.is_alive():
            print(f"[Tester] WARNING: Client timed out, killing...")
            p.terminate()
    
    elapsed = time.time() - start_time
    
    # Collect results
    results = []
    while not results_queue.empty():
        results.append(results_queue.get())
    
    success = sum(1 for r in results if r[1] == "OK")
    errors = [r for r in results if r[1] != "OK"]
    
    print(f"\n[Tester] Test completed in {elapsed:.2f} seconds")
    print(f"[Tester] Successful: {success}/{NUM_CLIENTS}")
    print(f"[Tester] Total data sent: {NUM_CLIENTS * BODY_SIZE / (1024*1024):.0f} MB")
    print(f"[Tester] Throughput: {(NUM_CLIENTS * BODY_SIZE) / elapsed / (1024*1024):.0f} MB/s")
    
    if errors:
        print(f"\n[Tester] Errors:")
        for client_id, status, _ in errors:
            print(f"  Client {client_id}: {status}")

def main():
    print("=" * 60)
    print("Heavy CGI Tester")
    print("=" * 60)
    print(f"Configuration:")
    print(f"  Host: {HOST}:{PORT}")
    print(f"  Endpoint: {ENDPOINT}")
    print(f"  Concurrent clients: {NUM_CLIENTS}")
    print(f"  Body size: {BODY_SIZE / (1024*1024):.0f} MB")
    print(f"  Total data: {NUM_CLIENTS * BODY_SIZE / (1024*1024):.0f} MB")
    print("=" * 60)
    
    # Check if server is running
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2)
        s.connect((HOST, PORT))
        s.close()
    except Exception as e:
        print(f"[Tester] ERROR: Cannot connect to server at {HOST}:{PORT}")
        print(f"[Tester] Make sure the webserver is running!")
        sys.exit(1)
    
    for run in range(NUM_RUNS):
        if NUM_RUNS > 1:
            print(f"\n{'='*60}")
            print(f"Run {run + 1}/{NUM_RUNS}")
            print(f"{'='*60}")
        run_test()
        if run < NUM_RUNS - 1:
            print("\n[Tester] Waiting 2 seconds before next run...")
            time.sleep(2)
    
    print("\n[Tester] All tests completed!")

if __name__ == "__main__":
    import socket  # Import here for the connection check
    main()