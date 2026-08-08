#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.10
PORT=17701
SRC1=239.255.7.20
SRC1_PORT=17702
SRC2=239.255.7.21
SRC2_PORT=17703

cap="$WORK/mpts_capture.ts"
report="$WORK/mpts_report.json"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 5000 \
    -O file "$cap" >"$WORK/tsp_mpts.log" 2>&1 &
TSPID=$!

timeout 6 "$BIN" -O lo -u -m $MCAST:$PORT \
    -i "udp://@$SRC1:$SRC1_PORT" -I lo --sid 101 -s "Channel One" \
    -i "udp://@$SRC2:$SRC2_PORT" -I lo --sid 102 -s "Channel Two" \
    >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!
sleep 0.5

ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 3 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$SRC1:$SRC1_PORT?localaddr=127.0.0.1&ttl=1"
ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=2000" -t 3 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$SRC2:$SRC2_PORT?localaddr=127.0.0.1&ttl=1"

wait $TVPID || true
wait $TSPID || true

[ -s "$cap" ] || fail "mpts: no packets captured (see $WORK/dipitvhead.log)"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze_mpts.log" \
    || fail "mpts: tsanalyze failed, see $WORK/tsanalyze_mpts.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "mpts: $cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "mpts: expected 2 services, got $services"

for name in "Channel One" "Channel Two"; do
    match=$(jq --arg n "$name" '[.services[] | select(.name == $n)] | length' "$report")
    [ "${match:-0}" -ge 1 ] || fail "mpts: expected service named '$name' not found"
done

echo "OK"

#EOF
