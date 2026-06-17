import threading
import time
import socket
import random
import http.client
import sys

TARGET_HOST = "127.0.0.1"
PORTS = [8080, 9090]
TEST_DURATION_SECONDS = 30
THREAD_COUNT_PER_PORT = 15

# Thread-safe printer lock to prevent text overlapping in the console
print_lock = threading.Lock()

def log_failure(port, method, path, status, context, headers=None):
    """Exclusively prints 5xx errors and structural drops with details."""
    with print_lock:
        print("\n" + "!" * 40)
        print(f"[ALERT] 5xx OR DROPPED CONNECTION DETECTED")
        print(f"Target:   {TARGET_HOST}:{port}")
        print(f"Request:  {method} {path}")
        print(f"Result:   Status {status} ({context})")
        if headers:
            print("Headers Received:")
            for h, v in headers:
                print(f"  {h}: {v}")
        print("!" * 40)

def execute_http_request(port, method, path, body=None, headers=None):
    if headers is None:
        headers = {"Host": "localhost"}
    else:
        headers["Host"] = "localhost"

    conn = http.client.HTTPConnection(TARGET_HOST, port, timeout=3.0)
    try:
        conn.request(method, path, body=body, headers=headers)
        response = conn.getresponse()
        status = response.status
        
        # We read the body to ensure we don't break the kernel buffer pipeline
        response_data = response.read() 
        
        # CRITICAL FILTER: Only log if it lands in the 5xx Internal Server Error range
        if 500 <= status < 600:
            log_failure(port, method, path, status, "Server Error Branch", response.getheaders())
            
    except Exception as e:
        # Log network drops/resets because they often happen when a 500 crash kills the worker process
        log_failure(port, method, path, "EXCEPTION", f"Network Drop: {str(e)}")
    finally:
        conn.close()

def send_malformed_request(port):
    """Raw TCP stream simulation."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(2.0)
        s.connect((TARGET_HOST, port))
        s.sendall(b"GET /unknown HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5000\r\n\r\n")
        time.sleep(0.01)
        s.close()
    except Exception as e:
        # Only log if the server actively refused it aggressively in a non-standard way
        if "refused" in str(e).lower() or "reset" in str(e).lower():
            log_failure(port, "RAW_TCP", "Malformed Payload", "DROP", str(e))

def worker_lifecycle(port, stop_event):
    # Expanded matrix mirroring your config file patterns
    paths_and_methods = [
        ("GET", "/"),
        ("DELETE", "/"),
        ("GET", "/dashboard/"),
        ("POST", "/cgi-bin/test.py"), 
        ("GET", "/cgi-bin/script.sh"), 
        ("GET", "/redirect/"),        
        ("DELETE", "/admin/"),        
        ("POST", "/uploads/"),        
        ("GET", "/errors/"),          
        ("GET", "/does-not-exist"),   
    ]
    
    large_payload = b"A" * (1024 * 1024 * 5)  # 5MB upload

    while not stop_event.is_set():
        scenario = random.randint(1, 4)
        
        if scenario == 1:
            method, path = random.choice(paths_and_methods)
            execute_http_request(port, method, path)
        elif scenario == 2:
            # Check if your CGI pipeline breaks under a POST load
            # execute_http_request(port, "POST", "/cgi-bin/test.py", body=b"param=value")
        elif scenario == 3:
            # Test if large file uploads cause 500 errors (e.g., out of memory or temp file creation failure)
            execute_http_request(port, "POST", "/uploads/stress.dat", body=large_payload)
        elif scenario == 4:
            send_malformed_request(port)

        time.sleep(random.uniform(0.005, 0.02))

def main():
    print("=" * 60)
    print("LAUNCHING EXCLUSIVE 5xx AUDITOR SUITE")
    print("Silence means success/4xx. Failures will stream live below:")
    print("=" * 60)

    stop_event = threading.Event()
    threads = []

    for port in PORTS:
        for _ in range(THREAD_COUNT_PER_PORT):
            t = threading.Thread(target=worker_lifecycle, args=(port, stop_event))
            t.daemon = True
            threads.append(t)
            t.start()

    try:
        time.sleep(TEST_DURATION_SECONDS)
    except KeyboardInterrupt:
        print("\nStopping auditor prematurely...")
    finally:
        stop_event.set()
        for t in threads:
            t.join(timeout=1.0)
        print("\nAudit session completed.")

if __name__ == "__main__":
    main()