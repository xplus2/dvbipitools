#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

HTTPPORT=19202

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 15 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    -metadata service_name="Stdin Channel" -metadata service_provider="Test" - 2>"$WORK/ffmpeg.log" \
    | timeout 15 "$BIN" -l "127.0.0.1:$HTTPPORT" -i - >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

cap="$WORK/capture.ts"
report="$WORK/report.json"
timeout 6 curl -s -o "$cap" "http://127.0.0.1:$HTTPPORT/stdin/ts"

kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

[ -s "$cap" ] || fail "no packets captured from /stdin/ts, see $WORK/dipixy.log"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" \
    || fail "tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "$cc_errors continuity-counter errors in capture"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Stdin Channel" ] || fail "expected service name 'Stdin Channel', got '$service_name'"

echo "OK"

#EOF
