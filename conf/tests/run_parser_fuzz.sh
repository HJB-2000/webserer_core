#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="$ROOT_DIR/webserv"
FUZZ_DIR="$ROOT_DIR/conf/confs/regression/fuzz_generated"

CASES="${1:-60}"
SEED="${2:-$RANDOM}"

mkdir -p "$FUZZ_DIR"

if [[ ! -x "$BIN" ]]; then
  echo "[info] webserv binary not found, building..."
  (cd "$ROOT_DIR" && make -j1) || {
    echo "[fatal] build failed"
    exit 2
  }
fi

random_pick() {
  # usage: random_pick "a" "b" "c"
  local idx=$((RANDOM % $# + 1))
  eval "echo \${$idx}"
}

gen_case() {
  local file="$1"

  local event_model
  event_model=$(random_pick "epoll" "poll" "select" "badmodel")

  local listen_val
  listen_val=$(random_pick "127.0.0.1:8080" "127.0.0.1:http" "99999" "127.0.0.1:65536" "0.0.0.0:80")

  local timeout_val
  timeout_val=$(random_pick "10" "60" "-1" "abc" "5000")

  local cgi_ext
  cgi_ext=$(random_pick ".py" ".php" "py")

  local cgi_path
  cgi_path=$(random_pick "/usr/bin/python3" "/bin/does-not-exist" "")

  local upload_methods
  upload_methods=$(random_pick "POST" "GET" "GET POST" "DELETE")

  local body_http
  body_http=$(random_pick "1M" "10..1M" "1Z" "2G")

  local body_loc
  body_loc=$(random_pick "50m" "0" "-1" "3K")

  local dup_events
  dup_events=$(random_pick "0" "1")

  local dup_allowed
  dup_allowed=$(random_pick "0" "1")

  local unclosed_loc
  unclosed_loc=$(random_pick "0" "1")

  local return_code
  return_code=$(random_pick "301" "302" "200" "404")

  local server_name_line
  server_name_line=$(random_pick "server_name fuzz.local;" "server_name fuzz.local www.fuzz.local;" "")

  {
    echo "events {"
    echo "  worker_connections 1;"
    echo "  event_model $event_model;"
    echo "}"

    if [[ "$dup_events" == "1" ]]; then
      echo "events { worker_connections 2; event_model epoll; }"
    fi

    echo
    echo "http {"
    echo "  client_max_body_size $body_http;"
    echo "  error_page 404 ./errors/404.html;"
    echo "  server {"
    echo "    listen $listen_val;"
    echo "    $server_name_line"
    echo "    root ./www/html;"
    echo "    index index.html;"
    echo "    timeout $timeout_val;"
    echo
    echo "    location / {"
    echo "      client_max_body_size $body_loc;"
    echo "      allowed_methods GET POST;"
    if [[ "$dup_allowed" == "1" ]]; then
      echo "      allowed_methods GET;"
    fi
    echo "      return $return_code /new;"
    echo "    }"
    echo
    echo "    location /cgi-bin/ {"
    echo "      allowed_methods GET POST;"
    echo "      cgi_ext $cgi_ext;"
    if [[ -n "$cgi_path" ]]; then
      echo "      cgi_path $cgi_path;"
    fi
    echo "    }"
    echo
    echo "    location /uploads/ {"
    echo "      allowed_methods $upload_methods;"
    echo "      upload_path ./www/uploads/;"
    echo "    }"

    if [[ "$unclosed_loc" == "0" ]]; then
      echo "  }"
      echo "}"
    else
      # intentionally malformed tail (unclosed braces)
      echo ""
      echo "# fuzz: intentionally unclosed blocks"
    fi
  } > "$file"
}

echo "== Parser Fuzz Runner =="
echo "Seed: $SEED"
echo "Cases: $CASES"
echo "Output dir: $FUZZ_DIR"
echo

RANDOM="$SEED"

PASS_PARSE_ERROR=0
PASS_OK=0
CRASH=0
OTHER=0

for ((i=1; i<=CASES; i++)); do
  CFG="$FUZZ_DIR/fuzz_$(printf "%03d" "$i").conf"
  OUT="/tmp/webserv_fuzz_$(printf "%03d" "$i").log"

  gen_case "$CFG"

  "$BIN" "$CFG" >"$OUT" 2>&1
  STATUS=$?

  if grep -Eq "AddressSanitizer|Segmentation fault|stack-buffer-overflow|ABORTING" "$OUT"; then
    echo "[CRASH] case=$i status=$STATUS cfg=$CFG"
    tail -n 8 "$OUT" | sed 's/^/        /'
    CRASH=$((CRASH + 1))
    continue
  fi

  if [[ "$STATUS" -eq 0 ]]; then
    PASS_OK=$((PASS_OK + 1))
  elif [[ "$STATUS" -eq 1 ]]; then
    # expected parser validation path for malformed fuzz cases
    PASS_PARSE_ERROR=$((PASS_PARSE_ERROR + 1))
  else
    echo "[OTHER] case=$i status=$STATUS cfg=$CFG"
    tail -n 6 "$OUT" | sed 's/^/        /'
    OTHER=$((OTHER + 1))
  fi
done

echo
echo "Summary:"
echo "  ok parses      : $PASS_OK"
echo "  parse errors   : $PASS_PARSE_ERROR"
echo "  crashes        : $CRASH"
echo "  other statuses : $OTHER"

if [[ "$CRASH" -gt 0 ]]; then
  exit 1
fi

exit 0
