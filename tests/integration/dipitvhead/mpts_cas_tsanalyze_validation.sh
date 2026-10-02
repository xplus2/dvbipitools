#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq nc; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.41.50
PORT=20710
SRC1=239.255.41.51
SRC1_PORT=20711
SRC2=239.255.41.52
SRC2_PORT=20712
ECMG_PORT=20713
EMMG_PORT=20714
ECMG_UP_S=30
CAS_UP_S=60
SCRAMBLE_WINDOW_S=9

cap="$WORK/mpts_cas_capture.ts"
report="$WORK/mpts_cas_report.json"
tvlog="$WORK/dipitvhead.log"

dump_logs() {
    tail -n 30 "$tvlog" "$WORK/tsecmg.log" "$WORK/tsp.log" "$WORK/ffmpeg1.log" "$WORK/ffmpeg2.log" >&2
}

cas_settled() {
    log_has "$tvlog" "channel+stream established" || ! kill -0 $TVPID 2>/dev/null
}

ECMGPID=
TSPID=
TVPID=
FF1PID=
FF2PID=
trap 'kill $FF1PID $FF2PID $TVPID $TSPID $ECMGPID 2>/dev/null; rm -rf "$WORK"' EXIT

gen_test_clip "$WORK/clip1.ts" 1000 3
gen_test_clip "$WORK/clip2.ts" 2000 3

tsecmg -p $ECMG_PORT -s --log-protocol=info >"$WORK/tsecmg.log" 2>&1 &
ECMGPID=$!
wait_until $ECMG_UP_S port_open $ECMG_PORT || fail "mpts cas: tsecmg never listened on $ECMG_PORT (see $WORK/tsecmg.log)"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!

"$BIN" -O lo -u -m $MCAST:$PORT \
    -i "udp://@$SRC1:$SRC1_PORT" -I lo --sid 101 -s "Channel One" \
    -i "udp://@$SRC2:$SRC2_PORT" -I lo --sid 102 -s "Channel Two" \
    --cas-algo csa2 --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750003 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 3000 \
    >"$tvlog" 2>&1 &
TVPID=$!

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$WORK/clip1.ts" -c copy -f mpegts \
    "udp://$SRC1:$SRC1_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg1.log" &
FF1PID=$!
ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$WORK/clip2.ts" -c copy -f mpegts \
    "udp://$SRC2:$SRC2_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg2.log" &
FF2PID=$!

wait_until $CAS_UP_S cas_settled
log_has "$tvlog" "channel+stream established" || { dump_logs; fail "mpts cas: ECMG channel not established"; }
sleep $SCRAMBLE_WINDOW_S

kill $FF1PID $FF2PID 2>/dev/null
wait $FF1PID $FF2PID 2>/dev/null || true
kill -INT $TVPID 2>/dev/null
wait $TVPID 2>/dev/null || true
kill $TSPID 2>/dev/null
wait $TSPID 2>/dev/null || true

[ -s "$cap" ] || { dump_logs; fail "mpts cas: no packets captured"; }

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" \
    || fail "mpts cas: tsanalyze failed, see $WORK/tsanalyze.log"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "mpts cas: expected 2 services, got $services"

for name in "Channel One" "Channel Two"; do
    scrambled=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0]["is-scrambled"]' "$report")
    [ "$scrambled" = "true" ] || { dump_logs; fail "mpts cas: '$name' not scrambled, is-scrambled=$scrambled"; }
    comps=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0].components.scrambled' "$report")
    [ "${comps:-0}" -ge 2 ] || fail "mpts cas: '$name' expected >=2 scrambled components, got $comps"
done

echo "OK"

#EOF
