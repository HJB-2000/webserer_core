#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MANIFEST="$ROOT_DIR/conf/tests/regression_manifest.txt"
BIN="$ROOT_DIR/webserv"

if [[ ! -x "$BIN" ]]; then
  echo "[info] webserv binary not found, building..."
  (cd "$ROOT_DIR" && make -j1) || {
    echo "[fatal] build failed"
    exit 2
  }
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "[fatal] manifest not found: $MANIFEST"
  exit 2
fi

PASS=0
FAIL=0

echo "== Parser Regression Suite =="
echo "Manifest: $MANIFEST"
echo

while IFS='|' read -r ID CFG EXPECT_EXIT EXPECT_REGEX BUG_AREA NOTES; do
  [[ -z "$ID" ]] && continue
  [[ "$ID" =~ ^# ]] && continue

  OUT_FILE="/tmp/webserv_reg_${ID}.log"
  "$BIN" "$ROOT_DIR/$CFG" >"$OUT_FILE" 2>&1
  STATUS=$?

  OK=1
  if [[ "$STATUS" -ne "$EXPECT_EXIT" ]]; then
    OK=0
  fi

  if [[ "$OK" -eq 1 && -n "$EXPECT_REGEX" ]]; then
    grep -E "$EXPECT_REGEX" "$OUT_FILE" >/dev/null 2>&1 || OK=0
  fi

  if [[ "$OK" -eq 1 ]]; then
    echo "[PASS] $ID - $NOTES"
    PASS=$((PASS + 1))
  else
    echo "[FAIL] $ID - $NOTES"
    echo "       expected exit=$EXPECT_EXIT, actual exit=$STATUS"
    echo "       suspicious area: $BUG_AREA"
    echo "       expected pattern: $EXPECT_REGEX"
    echo "       output excerpt:"
    tail -n 6 "$OUT_FILE" | sed 's/^/         /'
    FAIL=$((FAIL + 1))
  fi
done < "$MANIFEST"

echo
echo "Summary: PASS=$PASS FAIL=$FAIL"

if [[ "$FAIL" -ne 0 ]]; then
  exit 1
fi

exit 0
