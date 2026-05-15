import socket  
import time  
  
def send_chunked_request():  
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)  
    s.connect(('127.0.0.1', 8080))  
      
    # Send request headers with chunked encoding  
    request = (  
        "POST /upload HTTP/1.1\r\n"  
        "Host: 127.0.0.1\r\n"  
        "Transfer-Encoding: chunked\r\n"  
        "Content-Type: application/octet-stream\r\n"  
        "\r\n"  
    )  
    s.send(request.encode())  
      
    # Send 11MB in chunks (1MB chunks)  
    chunk_size = 1024 * 1024  # 1MB  
    total_sent = 0  
    target_size = 11 * 1024 * 1024  # 11MB  
      
    while total_sent < target_size:  
        remaining = target_size - total_sent  
        current_chunk = min(chunk_size, remaining)  
          
        # Send chunk size in hex  
        chunk_header = f"{current_chunk:X}\r\n"  
        s.send(chunk_header.encode())  
          
        # Send chunk data  
        chunk_data = b'A' * current_chunk  
        s.send(chunk_data)  
          
        # Send chunk trailer  
        s.send(b"\r\n")  
          
        total_sent += current_chunk  
        print(f"Sent {total_sent / (1024*1024):.1f}MB", end='\r')  
      
    # Send final chunk (0)  
    s.send(b"0\r\n\r\n")  
      
    # Receive response  
    response = b""  
    s.settimeout(5)  
    try:  
        while True:  
            data = s.recv(4096)  
            if not data:  
                break  
            response += data  
    except socket.timeout:  
        pass  
      
    s.close()  
      
    # Parse response code  
    response_str = response.decode('utf-8', errors='ignore')  
    if 'HTTP/1.1' in response_str:  
        code = response_str.split(' ')[1]  
        return code  
    return "000"  
  
if __name__ == "__main__":  
    code = send_chunked_request()  
    print(f"\nHTTP Response Code: {code}")  
    exit(0 if code == "413" else 1)  
EOF  
  
# Start server in background  
./webserv fahd.conf &  
SERVER_PID=$!  
sleep 2  
  
echo "Sending chunked POST request with 11MB body..."  
python3 send_chunked.py  
TEST_RESULT=$?  
  
# Check if server crashed  
if ! kill -0 $SERVER_PID 2>/dev/null; then  
    echo "❌ FAILED: Server crashed"  
    kill $SERVER_PID 2>/dev/null  
    rm -f send_chunked.py  
    exit 1  
fi  
  
if [ $TEST_RESULT -eq 0 ]; then  
    echo "✅ PASSED: Server returned 413 (Payload Too Large)"  
    echo "The check in _parseChunked is working correctly"  
else  
    echo "❌ FAILED: Server did not return 413"  
    echo "The check in _parseChunked is commented or not working"  
fi  
  
# Cleanup  
kill $SERVER_PID 2>/dev/null  
rm -f send_chunked.py
