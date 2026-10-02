#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
tsp -P pcredit --help >/dev/null 2>&1 || skip "tsp pcredit plugin not available"

MCAST=239.255.43.20
PORT=43720
KBPS=6000

cap="$WORK/regenerate_mpts.ts"
report="$WORK/regenerate_mpts.json"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 5000 \
    -O file "$cap" >"$WORK/tsp_capture.log" 2>&1 &
TSPID=$!

timeout 10 "$BIN" -O lo -u -m $MCAST:$PORT \
    -i "udp://@239.255.43.22:43722" -I lo --sid 101 -s "Channel One" \
    -i "udp://@239.255.43.24:43724" -I lo --sid 102 -s "Channel Two" \
    -b $KBPS -S -B --pcr-mode regenerate >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!
sleep 0.8

for n in 1 2; do
    raw=239.255.43.2$((n * 2 - 1))
    bad=239.255.43.2$((n * 2))
    tsp -I ip $raw:4372$((n * 2 - 1)) --local-address 127.0.0.1 --receive-timeout 4000 \
        -P pcredit --add-pcr 100000000 --random \
        -O ip $bad:4372$((n * 2)) --local-address 127.0.0.1 --ttl 1 >"$WORK/tsp_relay$n.log" 2>&1 &
done
sleep 0.3

for n in 1 2; do
    ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=$((n * 1000))" -t 5 \
        -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
        "udp://239.255.43.2$((n * 2 - 1)):4372$((n * 2 - 1))?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg$n.log" &
done
wait

[ -s "$cap" ] || fail "regenerate mpts: no packets captured (see $WORK/dipitvhead.log)"

verify=$(tsp -I file "$cap" -P pcrverify --pid 0x100 --pid 0x120 -O drop 2>&1 | grep "PCR OK")
ok=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\1/p')
bad=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\2/p')
[ "${ok:-0}" -ge 200 ] || fail "regenerate mpts: only ${ok:-0} verified PCRs ($verify)"
[ "${bad:-1}" = "0" ] || fail "regenerate mpts: $bad PCRs with jitter above 1 ms ($verify)"

missing=$(tsp -I file "$cap" -P continuity -O drop 2>&1 | grep -c "missing")
[ "$missing" = "0" ] || fail "regenerate mpts: $missing continuity errors"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "regenerate mpts: tsanalyze failed, see $WORK/tsanalyze.log"
services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "regenerate mpts: expected 2 services, got $services"
bitrate=$(jq '.ts["pcr-bitrate"]' "$report")
[ "${bitrate:-0}" -ge 5940000 ] && [ "${bitrate:-0}" -le 6060000 ] \
    || fail "regenerate mpts: PCR-derived bitrate $bitrate, expected 6000000 within 1%"

echo "OK"

#EOF
