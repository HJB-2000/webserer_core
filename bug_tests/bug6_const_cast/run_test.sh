#!/bin/bash
# BUG 6 — const_cast mutation test
# Compiles with UBSan, starts the server, fires the trigger, inspects output.
# Usage: ./run_test.sh [port]

PORT=${1:-5000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
UBSAN_LOG="/tmp/webserv_ubsan_bug6.log"

echo "========================================"
echo " BUG 6 — const_cast Mutation"
echo "========================================"

cd "$ROOT_DIR" || exit 1

echo "[*] Building with UBSan (-fsanitize=undefined)..."
make re CXXFLAGS="-g3 -std=c++98 -Wall -Wextra -fsanitize=undefined" \
    2>&1 | tail -5

if [ ! -x "./webserv" ]; then
    echo "[!] Build failed. Trying plain build..."
    make re -s 2>&1 | tail -3
    if [ ! -x "./webserv" ]; then
        echo "[!] Build failed."
        exit 1
    fi
fi

echo "[*] Starting server on port $PORT (stderr -> $UBSAN_LOG)..."
./webserv conf/confs/replit.conf 2>"$UBSAN_LOG" &
SERVER_PID=$!
sleep 0.5

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[!] Server failed to start. Check $UBSAN_LOG"
    cat "$UBSAN_LOG"
    exit 1
fi
echo "[*] Server PID: $SERVER_PID"
echo ""

# Run trigger
python3 "$SCRIPT_DIR/trigger.py" --host 127.0.0.1 --port "$PORT"

sleep 0.5
kill "$SERVER_PID" 2>/dev/null
wait "$SERVER_PID" 2>/dev/null

echo ""
echo "========================================"
echo " SERVER STDERR / UBSAN OUTPUT"
echo "========================================"
if [ -s "$UBSAN_LOG" ]; then
    grep -i "runtime error\|const_cast\|REMOTE_ADDR\|fs_path\|ResponseHandler" \
         "$UBSAN_LOG" | head -30
    echo ""
    if grep -q "runtime error" "$UBSAN_LOG"; then
        echo "[CONFIRMED] UBSan caught a runtime error — BUG 6 triggered."
    else
        echo "[INFO] No UBSan runtime error in this run."
        echo "       The const_cast is still bad practice — check path mutation"
        echo "       by diffing requested path vs path in server log."
    fi
else
    echo "(server produced no stderr output)"
fi
echo ""
echo "Full server log: $UBSAN_LOG"

# Rebuild normally to not leave UBSan binary
echo ""
echo "[*] Rebuilding without sanitizers (restoring normal binary)..."
make re -s 2>&1 | tail -2
