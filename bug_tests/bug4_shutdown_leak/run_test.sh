#!/bin/bash
# BUG 4 — Shutdown memory leak test via Valgrind
# Usage: ./run_test.sh [port]

PORT=${1:-5000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
CONF="conf/confs/replit.conf"
VALGRIND_LOG="/tmp/webserv_valgrind_bug4.log"

echo "========================================"
echo " BUG 4 — EventRef / CgiJob Leak on Shutdown"
echo "========================================"

cd "$ROOT_DIR" || exit 1

echo "[*] Building..."
make re -s 2>&1 | tail -3
[ ! -x "./webserv" ] && echo "[!] Build failed." && exit 1

if command -v valgrind &>/dev/null; then
    echo "[*] Starting server under Valgrind (log: $VALGRIND_LOG)..."
    valgrind \
        --leak-check=full \
        --track-origins=yes \
        --show-leak-kinds=definite,indirect \
        --log-file="$VALGRIND_LOG" \
        ./webserv "$CONF" &
    SERVER_PID=$!
else
    echo "[!] Valgrind not found — running without it."
    echo "    Install: apt-get install valgrind"
    ./webserv "$CONF" &
    SERVER_PID=$!
fi

sleep 1
kill -0 "$SERVER_PID" 2>/dev/null || { echo "[!] Server failed to start."; exit 1; }
echo "[*] Server PID: $SERVER_PID"
echo ""

# Run trigger in background
python3 "$SCRIPT_DIR/trigger.py" \
    --host 127.0.0.1 --port "$PORT" \
    --client-count 8 --cgi-count 5 &
TRIGGER_PID=$!

# Let trigger run for a few seconds then stop the server
sleep 6
echo ""
echo "[*] Sending SIGINT to server to trigger shutdown..."
kill -INT "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

# Also stop the trigger
kill "$TRIGGER_PID" 2>/dev/null
wait "$TRIGGER_PID" 2>/dev/null

if [ -f "$VALGRIND_LOG" ]; then
    echo ""
    echo "========================================"
    echo " VALGRIND LEAK REPORT"
    echo "========================================"
    grep -E "LEAK SUMMARY|definitely lost|_registerEventFd|_startCgi|EventRef|CgiJob|HEAP SUMMARY" \
         "$VALGRIND_LOG" | head -30
    echo ""
    echo "Full report: $VALGRIND_LOG"

    DEFINITE=$(grep "definitely lost:" "$VALGRIND_LOG" | grep -v " 0 bytes")
    if [ -n "$DEFINITE" ]; then
        echo ""
        echo "[CONFIRMED] Definite leaks found — BUG 4 is present."
    else
        echo ""
        echo "[NOT CONFIRMED] No definite leaks detected in this run."
    fi
fi
