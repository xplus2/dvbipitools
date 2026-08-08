#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.9
PORT=17900

rec="$WORK/rec.ts"
report="$WORK/report.json"

"$BIN" -i "udp://@$MCAST:$PORT" -o "$rec" -f ts -t 4 -I lo >"$WORK/dipirec.log" 2>&1 &
RECPID=$!
sleep 0.5

ffmpeg -hide_banner -loglevel error -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 3 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$PORT?localaddr=127.0.0.1&ttl=1"

wait $RECPID || true

[ -s "$rec" ] || fail "dipirec: no recording produced (see $WORK/dipirec.log, $WORK/tsp_send.log)"

tsanalyze --json "$rec" > "$report" 2>"$WORK/tsanalyze.log" \
    || fail "dipirec: tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "dipirec: $cc_errors continuity-counter errors in recording"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 1 ] || fail "dipirec: no services found in recorded output"

echo "OK"

#EOF
