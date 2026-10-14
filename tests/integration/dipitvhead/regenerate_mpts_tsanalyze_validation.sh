#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
tsp -P pcredit --help >/dev/null 2>&1 || skip "tsp pcredit plugin not available"

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 5)
PORT=$((FPB + 0))
RAW1=$(unique_mcast 63)
BAD1=$(unique_mcast 64)
RAW2=$(unique_mcast 65)
BAD2=$(unique_mcast 66)
RAW1_PORT=$((FPB + 1))
BAD1_PORT=$((FPB + 2))
RAW2_PORT=$((FPB + 3))
BAD2_PORT=$((FPB + 4))
KBPS=6000

cap="$WORK/regenerate_mpts.ts"
report="$WORK/regenerate_mpts.json"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 60000 \
    -O file "$cap" >"$WORK/tsp_capture.log" 2>&1 &
TSPID=$!
wait_until 30 udp_port_busy $PORT || fail "regenerate mpts: tsp capture never bound $PORT"

timeout 70 "$BIN" -O lo -u -m $MCAST:$PORT \
    -i "udp://@$BAD1:$BAD1_PORT" -I lo --sid 101 -s "Channel One" \
    -i "udp://@$BAD2:$BAD2_PORT" -I lo --sid 102 -s "Channel Two" \
    -b $KBPS -S -B --pcr-mode regenerate >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!
wait_until 30 udp_port_busy $BAD1_PORT || fail "regenerate mpts: receiver never bound $BAD1_PORT"
wait_until 30 udp_port_busy $BAD2_PORT || fail "regenerate mpts: receiver never bound $BAD2_PORT"

RELAYPIDS=
for n in 1 2; do
    eval "raw=\$RAW$n bad=\$BAD$n raw_port=\$RAW${n}_PORT bad_port=\$BAD${n}_PORT"
    tsp -I ip $raw:$raw_port --local-address 127.0.0.1 --receive-timeout 60000 \
        -P pcredit --add-pcr 100000000 --random -P continuity --fix \
        -O ip $bad:$bad_port --local-address 127.0.0.1 --ttl 1 >"$WORK/tsp_relay$n.log" 2>&1 &
    RELAYPIDS="$RELAYPIDS $!"
done
wait_until 30 udp_port_busy $RAW1_PORT || fail "regenerate mpts: relay never bound $RAW1_PORT"
wait_until 30 udp_port_busy $RAW2_PORT || fail "regenerate mpts: relay never bound $RAW2_PORT"

FFPIDS=
for n in 1 2; do
    eval "raw=\$RAW$n raw_port=\$RAW${n}_PORT"
    ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=$((n * 1000))" -t 5 \
        -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
        "udp://$raw:$raw_port?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg$n.log" &
    FFPIDS="$FFPIDS $!"
done
wait $FFPIDS

sleep 0.5
kill $RELAYPIDS 2>/dev/null
wait $RELAYPIDS 2>/dev/null || true
sleep 0.5
kill $TVPID 2>/dev/null
wait $TVPID 2>/dev/null || true
kill $TSPID 2>/dev/null
wait $TSPID 2>/dev/null || true

[ -s "$cap" ] || fail "regenerate mpts: no packets captured (see $WORK/dipitvhead.log)"

verify=$(tsp -I file "$cap" -P pcrverify --pid 0x100 --pid 0x120 -O drop 2>&1 | grep "PCR OK" | sed 's/\([0-9]\),\([0-9]\)/\1\2/g')
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
