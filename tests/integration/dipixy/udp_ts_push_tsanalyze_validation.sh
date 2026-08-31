#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.9.20
MPORT=18100
HTTPPORT=19200

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 20 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    -metadata service_name="Test Channel" -metadata service_provider="Test" \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 15 "$BIN" -l "127.0.0.1:$HTTPPORT" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

cap="$WORK/capture.ts"
report="$WORK/report.json"

timeout 6 curl -s -o "$cap" "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/ts"

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

[ -s "$cap" ] || fail "no packets captured, see $WORK/dipixy.log and $WORK/ffmpeg.log"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" \
    || fail "tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "$cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 1 ] || fail "no services found in captured output"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Test Channel" ] || fail "expected service name 'Test Channel', got '$service_name'"

echo "OK"

#EOF
