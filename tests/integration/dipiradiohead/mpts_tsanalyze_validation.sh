#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq python3; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.8
PORT=17800
HTTP_PORT=18080

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=3" \
    -c:a libmp3lame -f mp3 "$WORK/stream.mp3"

mkdir -p "$WORK/httproot"
cp "$WORK/stream.mp3" "$WORK/httproot/stream.mp3"

cap="$WORK/spts_capture.ts"
report="$WORK/spts_report.json"

(cd "$WORK/httproot" && python3 -u -m http.server "$HTTP_PORT" --bind 127.0.0.1 \
    >"$WORK/httpd.log" 2>&1) &
HTTPD=$!
trap 'kill $HTTPD 2>/dev/null; rm -rf "$WORK"' EXIT
i=0
while ! grep -q "Serving HTTP" "$WORK/httpd.log" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 200 ] || fail "spts: http server on $HTTP_PORT never became ready"
    sleep 0.1
done

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap" >"$WORK/tsp_spts.log" 2>&1 &
TSPID=$!
sleep 0.3

timeout 20 "$BIN" -I lo -m $MCAST:$PORT -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "Test Station" || true

wait $TSPID || true
kill $HTTPD 2>/dev/null || true

[ -s "$cap" ] || fail "spts: no packets captured"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze_spts.log" || fail "spts: tsanalyze failed, see $WORK/tsanalyze_spts.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "spts: $cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 1 ] || fail "spts: no services found in captured output"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Test Station" ] || fail "spts: expected service name 'Test Station', got '$service_name'"

echo "OK"

#EOF
