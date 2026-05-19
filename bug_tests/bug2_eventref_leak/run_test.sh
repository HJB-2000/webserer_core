#!/bin/bash
# BUG 2 — Run test under Valgrind
# Usage: ./run_test.sh [port]

PORT=${1:-5000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CONF="conf/confs/replit.conf"
VALGRIND_LOG="/tmp/webserv_valgrind_bug2.log"

echo "========================================"
echo " BUG 2 — Memory Leak in _registerEventFd"
echo "========================================"

cd "$ROOT_DIR" || exit 1

echo "[*] Building..."
make re -s 2>&1 | tail -3
if [ ! -x "./webserv" ]; then
    echo "[!] Build failed."
    exit 1
fi

if ! command -v valgrind &>/dev/null; then
    echo "[!] Valgrind not found. Running without Valgrind."
    echo "    Install with: apt-get install valgrind"
    echo ""
    echo "[*] Starting server normally..."
    ./webserv "$CONF" &
    SERVER_PID=$!
else
    echo "[*] Starting server under Valgrind (log: $VALGRIND_LOG)..."
    valgrind \
        --leak-check=full \
        --track-origins=yes \
        --log-file="$VALGRIND_LOG" \
        ./webserv "$CONF" &
    SERVER_PID=$!
fi

sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[!] Server failed to start."
    exit 1
fi

echo "[*] Server PID: $SERVER_PID"
echo ""

# Run the trigger
python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT"

echo ""
echo "[*] Sending SIGINT to server to flush Valgrind report..."
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

if [ -f "$VALGRIND_LOG" ]; then
    echo ""
    echo "========================================"
    echo " VALGRIND LEAK SUMMARY"
    echo "========================================"
    grep -A 5 "LEAK SUMMARY\|definitely lost\|_registerEventFd\|EventRef" \
         "$VALGRIND_LOG" | head -40
    echo ""
    echo "Full Valgrind report: $VALGRIND_LOG"
fi
