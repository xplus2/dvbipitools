#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.40
PORT=17740
ECMG_PORT=12240
EMMG_PORT=18010

cap="$WORK/cas_capture.ts"
report="$WORK/cas_report.json"

tsecmg -p $ECMG_PORT -s --log-protocol=info >"$WORK/tsecmg.log" 2>&1 &
ECMGPID=$!
sleep 0.3

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 10 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout 12 "$BIN" -O lo -u -m $MCAST:$PORT -i - -s "CAS Test" \
    --cas-algo cissa --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750002 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 3000 \
    >"$WORK/dipitvhead.log" 2>&1

wait $TSPID || true
kill $ECMGPID 2>/dev/null

[ -s "$cap" ] || fail "cas: no packets captured (see $WORK/dipitvhead.log)"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze.log" \
    || fail "cas: tsanalyze failed, see $WORK/tsanalyze.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report")
[ "$is_scrambled" = "true" ] || fail "cas: expected scrambled output, is-scrambled=$is_scrambled"

scrambled_count=$(jq -r '.services[0].components.scrambled' "$report")
[ "${scrambled_count:-0}" -ge 2 ] || fail "cas: expected >=2 scrambled components (video+audio), got $scrambled_count"

echo "OK"

#EOF
