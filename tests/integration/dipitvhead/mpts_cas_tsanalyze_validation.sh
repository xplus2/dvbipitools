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
WAIT_TICKS=600

cap="$WORK/mpts_cas_capture.ts"
snap="$WORK/mpts_cas_snapshot.ts"
report="$WORK/mpts_cas_report.json"

wait_port() {
    wp_port=$1
    i=0
    while [ $i -lt $WAIT_TICKS ]; do
        nc -z 127.0.0.1 "$wp_port" >/dev/null 2>&1 && return 0
        i=$((i + 1))
        sleep 0.1
    done
    return 1
}

both_scrambled() {
    for name in "Channel One" "Channel Two"; do
        s=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0]["is-scrambled"]' "$1" 2>/dev/null)
        c=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0].components.scrambled' "$1" 2>/dev/null)
        [ "$s" = "true" ] && [ "${c:-0}" -ge 2 ] 2>/dev/null || return 1
    done
    return 0
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
wait_port $ECMG_PORT || fail "mpts cas: tsecmg never listened on $ECMG_PORT (see $WORK/tsecmg.log)"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!

"$BIN" -O lo -u -m $MCAST:$PORT \
    -i "udp://@$SRC1:$SRC1_PORT" -I lo --sid 101 -s "Channel One" \
    -i "udp://@$SRC2:$SRC2_PORT" -I lo --sid 102 -s "Channel Two" \
    --cas-algo csa2 --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750003 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 3000 \
    >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$WORK/clip1.ts" -c copy -f mpegts \
    "udp://$SRC1:$SRC1_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg1.log" &
FF1PID=$!
ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -i "$WORK/clip2.ts" -c copy -f mpegts \
    "udp://$SRC2:$SRC2_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg2.log" &
FF2PID=$!

i=0
while [ $i -lt $WAIT_TICKS ]; do
    kill -0 $TVPID 2>/dev/null || fail "mpts cas: dipitvhead exited early (see $WORK/dipitvhead.log)"
    if [ -s "$cap" ]; then
        cp "$cap" "$snap"
        tsanalyze --json "$snap" >"$report" 2>/dev/null && both_scrambled "$report" && break
    fi
    i=$((i + 1))
    sleep 0.5
done

kill $FF1PID $FF2PID 2>/dev/null
wait $FF1PID $FF2PID 2>/dev/null || true
kill -INT $TVPID 2>/dev/null
wait $TVPID 2>/dev/null || true
kill $TSPID 2>/dev/null
wait $TSPID 2>/dev/null || true

[ -s "$cap" ] || fail "mpts cas: no packets captured (see $WORK/dipitvhead.log)"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze.log" \
    || fail "mpts cas: tsanalyze failed, see $WORK/tsanalyze.log"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "mpts cas: expected 2 services, got $services"

for name in "Channel One" "Channel Two"; do
    scrambled=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0]["is-scrambled"]' "$report")
    [ "$scrambled" = "true" ] || fail "mpts cas: '$name' not scrambled, is-scrambled=$scrambled"
    comps=$(jq -r --arg n "$name" '[.services[] | select(.name == $n)][0].components.scrambled' "$report")
    [ "${comps:-0}" -ge 2 ] || fail "mpts cas: '$name' expected >=2 scrambled components, got $comps"
done

echo "OK"

#EOF
