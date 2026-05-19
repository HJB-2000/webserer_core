#!/bin/bash
# BUG 5 — IPv4 cast test
# Usage: ./run_test.sh [port]

PORT=${1:-5000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================"
echo " BUG 5 — addrToString IPv4-Only Cast"
echo "========================================"

cd "$ROOT_DIR" || exit 1

make re -s 2>&1 | tail -3
[ ! -x "./webserv" ] && echo "[!] Build failed." && exit 1

echo "[*] Starting server on port $PORT..."
./webserv conf/confs/replit.conf &
SERVER_PID=$!
sleep 0.5

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[!] Server failed to start."
    exit 1
fi
echo "[*] Server PID: $SERVER_PID"
echo ""

# Run with IPv6 flag if the system has IPv6 loopback
if ip addr 2>/dev/null | grep -q "::1"; then
    python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT" --ipv6
else
    echo "[*] IPv6 not detected on this system. Running IPv4-only test."
    python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT"
fi

echo ""
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
echo "[*] Server stopped."
