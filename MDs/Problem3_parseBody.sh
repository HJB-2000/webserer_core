#!/bin/bash  
  
echo "=== Test 1: Regular Body Size Limit (_parseBody) ==="  
echo "Location limit: 10M"  
echo "Test body size: 11MB (exceeds limit)"  
echo ""  
  
# Create a test file with 11MB data  
dd if=/dev/zero of=big_body.bin bs=1M count=11 2>/dev/null  
  
# Start server in background  
./webserv fahd.conf &  
SERVER_PID=$!  
sleep 2  
  
echo "Sending POST request with 11MB body..."  
RESPONSE=$(curl -s -w "\n%{http_code}" -X POST \  
  -H "Content-Type: application/octet-stream" \  
  -H "Content-Length: 11534336" \  
  --data-binary @big_body.bin \  
  http://127.0.0.1:8080/upload)  
  
HTTP_CODE=$(echo "$RESPONSE" | tail -n1)  
BODY=$(echo "$RESPONSE" | head -n-1)  
  
echo "HTTP Response Code: $HTTP_CODE"  
  
# Check if server crashed  
if ! kill -0 $SERVER_PID 2>/dev/null; then  
    echo "❌ FAILED: Server crashed"  
    kill $SERVER_PID 2>/dev/null  
    rm -f big_body.bin  
    exit 1  
fi  
  
if [ "$HTTP_CODE" = "413" ]; then  
    echo "✅ PASSED: Server returned 413 (Payload Too Large)"  
    echo "The check in _parseBody is working correctly"  
else  
    echo "❌ FAILED: Server returned $HTTP_CODE instead of 413"  
    echo "The check in _parseBody is commented or not working"  
fi  
  
# Cleanup  
kill $SERVER_PID 2>/dev/null  
rm -f big_body.bin
