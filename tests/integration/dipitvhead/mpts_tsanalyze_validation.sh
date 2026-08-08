#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.7
PORT=17700

gen_clip() {
    ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=$2" -t 3 \
        -c:v libx264 -preset ultrafast -c:a aac -f mpegts "$1"
}

clip1="$WORK/clip1.ts"
gen_clip "$clip1" 1000

cap="$WORK/spts_capture.ts"
report="$WORK/spts_report.json"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 4000 \
    -O file "$cap" >"$WORK/tsp_spts.log" 2>&1 &
TSPID=$!
sleep 0.3

"$BIN" -O lo -u -m $MCAST:$PORT -i - -s "Test Channel" < "$clip1"

wait $TSPID || true

[ -s "$cap" ] || fail "spts: no packets captured"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze_spts.log" \
    || fail "spts: tsanalyze failed, see $WORK/tsanalyze_spts.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "spts: $cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 1 ] || fail "spts: no services found in captured output"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Test Channel" ] || fail "spts: expected service name 'Test Channel', got '$service_name'"

echo "OK"

#EOF
