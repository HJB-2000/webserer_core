#!/bin/bash
# BUG 3 — Run test
# Usage: ./run_test.sh [port]

PORT=${1:-5000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================"
echo " BUG 3 — EPOLLOUT dropped on EPOLLIN+EPOLLOUT"
echo "========================================"

cd "$ROOT_DIR" || exit 1

echo "[*] Building..."
make re -s 2>&1 | tail -3
[ ! -x "./webserv" ] && echo "[!] Build failed." && exit 1

echo "[*] Starting server on port $PORT..."
./webserv conf/confs/replit.conf &
SERVER_PID=$!
sleep 0.5

kill -0 "$SERVER_PID" 2>/dev/null || { echo "[!] Server failed to start."; exit 1; }
echo "[*] Server PID: $SERVER_PID"
echo ""

python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT"

echo ""
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null
echo "[*] Server stopped."
