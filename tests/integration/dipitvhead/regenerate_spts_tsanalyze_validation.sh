#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
tsp -P pcredit --help >/dev/null 2>&1 || skip "tsp pcredit plugin not available"

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 3)
PORT=$((FPB + 0))
RAW=$(unique_mcast 63)
RAW_PORT=$((FPB + 1))
BAD=$(unique_mcast 64)
BAD_PORT=$((FPB + 2))
KBPS=3000

cap="$WORK/regenerate_spts.ts"
report="$WORK/regenerate_spts.json"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 5000 \
    -O file "$cap" >"$WORK/tsp_capture.log" 2>&1 &
TSPID=$!

timeout 10 "$BIN" -O lo -u -m $MCAST:$PORT -i "udp://@$BAD:$BAD_PORT" -I lo \
    -b $KBPS -S -B --pcr-mode regenerate >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!
sleep 0.5

tsp -I ip $RAW:$RAW_PORT --local-address 127.0.0.1 --receive-timeout 4000 \
    -P pcredit --add-pcr 100000000 --random -P continuity --fix \
    -O ip $BAD:$BAD_PORT --local-address 127.0.0.1 --ttl 1 >"$WORK/tsp_relay.log" 2>&1 &
RELAYPID=$!
sleep 0.3

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 5 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$RAW:$RAW_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg.log"

wait $TVPID || true
wait $RELAYPID || true
wait $TSPID || true

[ -s "$cap" ] || fail "regenerate spts: no packets captured (see $WORK/dipitvhead.log)"

verify=$(tsp -I file "$cap" -P pcrverify --pid 0x100 -O drop 2>&1 | grep "PCR OK")
ok=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\1/p')
bad=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\2/p')
[ "${ok:-0}" -ge 100 ] || fail "regenerate spts: only ${ok:-0} verified PCRs ($verify)"
[ "${bad:-1}" = "0" ] || fail "regenerate spts: $bad PCRs with jitter above 1 ms ($verify)"

missing=$(tsp -I file "$cap" -P continuity -O drop 2>&1 | grep -c "missing")
[ "$missing" = "0" ] || fail "regenerate spts: $missing continuity errors"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "regenerate spts: tsanalyze failed, see $WORK/tsanalyze.log"
bitrate=$(jq '.ts["pcr-bitrate"]' "$report")
[ "${bitrate:-0}" -ge 2970000 ] && [ "${bitrate:-0}" -le 3030000 ] \
    || fail "regenerate spts: PCR-derived bitrate $bitrate, expected 3000000 within 1%"

echo "OK"

#EOF
