#!/bin/bash
# BUG 1 — Run test
# Usage: ./run_test.sh [port]
#
# Starts the server in the background, runs the trigger script,
# then reports whether the server survived.

PORT=${1:-5000}
SERVER_BIN="./webserv"
CONF="conf/confs/replit.conf"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "========================================"
echo " BUG 1 — UAF in _startCgi"
echo "========================================"

cd "$ROOT_DIR" || exit 1

# Build
echo "[*] Building..."
make re -s 2>&1 | tail -5
if [ ! -x "$SERVER_BIN" ]; then
    echo "[!] Build failed."
    exit 1
fi

# Start server
echo "[*] Starting server on port $PORT..."
"$SERVER_BIN" "$CONF" &
SERVER_PID=$!
sleep 0.5

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[!] Server failed to start."
    exit 1
fi

echo "[*] Server PID: $SERVER_PID"
echo ""

# Run trigger
python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT" --rounds 100

echo ""
# Final alive check
if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[RESULT] Server still running — bug not triggered in this run."
    kill "$SERVER_PID" 2>/dev/null
else
    echo "[RESULT] Server process is GONE — BUG 1 confirmed."
fi
