#!/usr/bin/env bash
# End-to-end smoke test: starts the server, pokes it over real sockets.
# Usage: tests/smoke.sh [path-to-binary]   (needs bash, curl)
set -u

BIN=${1:-./httpd-c}
PORT=${PORT:-$((20000 + RANDOM % 20000))}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOG=$(mktemp)
FAILS=0
CHECKS=0

check() { # check <description> <command...>
  local desc=$1
  shift
  CHECKS=$((CHECKS + 1))
  if "$@" >/dev/null 2>&1; then
    echo "  ok   $desc"
  else
    echo "  FAIL $desc"
    FAILS=$((FAILS + 1))
  fi
}

# raw <request-bytes> -> prints whatever the server sends back until it closes
raw() {
  exec 3<>"/dev/tcp/127.0.0.1/$PORT" || return 1
  printf "$1" >&3
  timeout_cat 3
  exec 3<&- 3>&-
}
timeout_cat() { # read fd $1 until EOF or 3s of silence
  local line
  while IFS= read -r -t 3 line <&"$1"; do printf '%s\n' "$line"; done
}

oversized() { # a request whose header block exceeds the server's 16 KiB limit
  exec 3<>"/dev/tcp/127.0.0.1/$PORT" || return 1
  { printf 'GET / HTTP/1.1\r\nHost: x\r\nX-Big: '; head -c 17000 /dev/zero | tr '\0' a; printf '\r\n\r\n'; } >&3
  timeout_cat 3
  exec 3<&- 3>&-
}

export PORT
export -f raw timeout_cat oversized

http_code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

APP_VERSION=smoke-1.2.3 WWW_ROOT="$ROOT/www" "$BIN" "$PORT" >"$LOG" 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null; rm -f "$LOG"' EXIT

for _ in $(seq 1 50); do
  curl -s -o /dev/null "localhost:$PORT/health" && break
  sleep 0.1
done

BASE="http://127.0.0.1:$PORT"
echo "httpd-c smoke test on port $PORT"

echo "routes"
check "GET / serves index.html"        bash -c "curl -s $BASE/ | grep -q '<title>httpd-c</title>'"
check "GET /health is 200 ok"          bash -c "curl -s $BASE/health | grep -q '\"status\":\"ok\"'"
check "GET /version reports APP_VERSION" bash -c "curl -s $BASE/version | grep -q 'smoke-1.2.3'"
check "GET /nope is 404"               test "$(http_code $BASE/nope)" = 404
check "query string is ignored by routing" test "$(http_code "$BASE/health?x=1")" = 200
check "POST is 405 with Allow"         bash -c "curl -s -i -X POST $BASE/ | grep -qi '^Allow: GET, HEAD'"
check "HEAD has Content-Length, no body" bash -c "r=\$(raw 'HEAD /health HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n'); echo \"\$r\" | grep -q '^Content-Length: 16' && ! echo \"\$r\" | grep -q status"

echo "static file safety"
check "../ traversal is 404"           test "$(http_code --path-as-is $BASE/../Makefile)" = 404
check "%2e%2e traversal is 404"        test "$(http_code --path-as-is $BASE/%2e%2e/Makefile)" = 404
check "encoded slash traversal is 404" test "$(http_code --path-as-is "$BASE/..%2fMakefile")" = 404

echo "protocol"
check "garbage request line is 400"   bash -c "raw 'BAD\r\n\r\n' | head -1 | grep -q ' 400 '"
check "HTTP/1.1 without Host is 400"  bash -c "raw 'GET / HTTP/1.1\r\n\r\n' | head -1 | grep -q ' 400 '"
check "HTTP/2.0 is 505"                bash -c "raw 'GET / HTTP/2.0\r\nHost: x\r\n\r\n' | head -1 | grep -q ' 505 '"
check "chunked request is 501"        bash -c "raw 'POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n' | head -1 | grep -q ' 501 '"
check "oversized headers are 431"     bash -c "oversized | head -1 | grep -q ' 431 '"
check "keep-alive reuses connection"  bash -c "curl -s -w '%{num_connects}\n' -o /dev/null $BASE/health -o /dev/null $BASE/health | tail -1 | grep -qx 0"
check "Connection: close is honoured" bash -c "curl -s -i -H 'Connection: close' $BASE/health | grep -qi '^Connection: close'"
check "pipelined requests both answered" bash -c "test \$(raw 'GET /health HTTP/1.1\r\nHost: x\r\n\r\nGET /version HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' | grep -c '^HTTP/1.1 200') = 2"
check "request split across packets"  bash -c "exec 3<>/dev/tcp/127.0.0.1/$PORT; printf 'GET /hea' >&3; sleep 0.3; printf 'lth HTTP/1.1\r\nHo' >&3; sleep 0.3; printf 'st: x\r\nConnection: close\r\n\r\n' >&3; read -r -t 3 line <&3; echo \$line | grep -q ' 200 '"

echo "concurrency"
exec 4<>"/dev/tcp/127.0.0.1/$PORT" # idle client that never sends a byte
check "idle connection doesn't block others" test "$(http_code --max-time 2 $BASE/health)" = 200
exec 4<&- 4>&-

echo "metrics"
check "exposes request counters"       bash -c "curl -s $BASE/metrics | grep -q 'httpd_requests_total{code=\"404\"} [1-9]'"
check "exposes latency histogram"      bash -c "curl -s $BASE/metrics | grep -q 'httpd_request_duration_seconds_bucket{le=\"+Inf\"}'"
check "exposes build info"             bash -c "curl -s $BASE/metrics | grep -q 'httpd_build_info{version=\"smoke-1.2.3\"} 1'"

echo "shutdown"
kill -TERM $PID
for _ in $(seq 1 40); do kill -0 $PID 2>/dev/null || break; sleep 0.1; done
check "exits promptly on SIGTERM"      bash -c "! kill -0 $PID 2>/dev/null"
wait $PID 2>/dev/null
check "exit status is 0"               test $? -eq 0
check "access log written to stdout"   grep -q '"GET /health HTTP/1.1" 200' "$LOG"

echo
if [ "$FAILS" -eq 0 ]; then
  echo "all $CHECKS checks passed"
else
  echo "$FAILS of $CHECKS checks FAILED"
  echo "--- server log ---"
  cat "$LOG"
  exit 1
fi
