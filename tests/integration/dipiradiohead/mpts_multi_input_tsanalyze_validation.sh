#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq python3; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.11
PORT=17801
HTTP_PORT1=18081
HTTP_PORT2=18082

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=3" \
    -c:a libmp3lame -f mp3 "$WORK/stream1.mp3"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=2000:duration=3" \
    -c:a libmp3lame -f mp3 "$WORK/stream2.mp3"

mkdir -p "$WORK/httproot1" "$WORK/httproot2"
cp "$WORK/stream1.mp3" "$WORK/httproot1/stream.mp3"
cp "$WORK/stream2.mp3" "$WORK/httproot2/stream.mp3"

cap="$WORK/mpts_capture.ts"
report="$WORK/mpts_report.json"

(cd "$WORK/httproot1" && python3 -u -m http.server "$HTTP_PORT1" --bind 127.0.0.1 \
    >"$WORK/httpd1.log" 2>&1) &
HTTPD1=$!
(cd "$WORK/httproot2" && python3 -u -m http.server "$HTTP_PORT2" --bind 127.0.0.1 \
    >"$WORK/httpd2.log" 2>&1) &
HTTPD2=$!
trap 'kill $HTTPD1 $HTTPD2 2>/dev/null; rm -rf "$WORK"' EXIT
i=0
while ! grep -q "Serving HTTP" "$WORK/httpd1.log" 2>/dev/null || \
      ! grep -q "Serving HTTP" "$WORK/httpd2.log" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 200 ] || fail "mpts: http server(s) never became ready"
    sleep 0.1
done

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap" >"$WORK/tsp_mpts.log" 2>&1 &
TSPID=$!
sleep 0.3

timeout 20 "$BIN" -I lo -m $MCAST:$PORT \
    -i "http://127.0.0.1:$HTTP_PORT1/stream.mp3" --sid 101 -s "Station One" \
    -i "http://127.0.0.1:$HTTP_PORT2/stream.mp3" --sid 102 -s "Station Two" || true

wait $TSPID || true
kill $HTTPD1 $HTTPD2 2>/dev/null || true

[ -s "$cap" ] || fail "mpts: no packets captured"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze_mpts.log" || fail "mpts: tsanalyze failed, see $WORK/tsanalyze_mpts.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "mpts: $cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "mpts: expected 2 services, got $services"

for name in "Station One" "Station Two"; do
    match=$(jq --arg n "$name" '[.services[] | select(.name == $n)] | length' "$report")
    [ "${match:-0}" -ge 1 ] || fail "mpts: expected service named '$name' not found"
done

echo "OK"

#EOF
