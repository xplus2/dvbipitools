#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq nc; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
tsp -P pcredit --help >/dev/null 2>&1 || skip "tsp pcredit plugin not available"

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 5)
PORT=$((FPB + 0))
RAW=$(unique_mcast 63)
RAW_PORT=$((FPB + 1))
BAD=$(unique_mcast 64)
BAD_PORT=$((FPB + 2))
ECMG_PORT=$((FPB + 3))
EMMG_PORT=$((FPB + 4))
KBPS=3000
ECMG_UP_S=30
CAS_UP_S=60
SCRAMBLE_WINDOW_S=6

cap="$WORK/regenerate_cas.ts"
report="$WORK/regenerate_cas.json"
tvlog="$WORK/dipitvhead.log"

dump_logs() {
    tail -n 30 "$tvlog" "$WORK/tsecmg.log" "$WORK/tsp_relay.log" "$WORK/ffmpeg.log" >&2
}

cas_settled() {
    log_has "$tvlog" "channel+stream established" || ! kill -0 $TVPID 2>/dev/null
}

ECMGPID=
TSPID=
TVPID=
RELAYPID=
FFPID=
trap 'kill $FFPID $RELAYPID $TVPID $TSPID $ECMGPID 2>/dev/null; rm -rf "$WORK"' EXIT

tsecmg -p $ECMG_PORT -s --log-protocol=info >"$WORK/tsecmg.log" 2>&1 &
ECMGPID=$!
wait_until $ECMG_UP_S port_open $ECMG_PORT || fail "regenerate cas: tsecmg never listened on $ECMG_PORT (see $WORK/tsecmg.log)"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 5000 \
    -O file "$cap" >"$WORK/tsp_capture.log" 2>&1 &
TSPID=$!

"$BIN" -O lo -u -m $MCAST:$PORT -i "udp://@$BAD:$BAD_PORT" -I lo \
    -b $KBPS -S -B --pcr-mode regenerate \
    --cas-algo csa2 --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750003 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 2000 >"$tvlog" 2>&1 &
TVPID=$!

tsp -I ip $RAW:$RAW_PORT --local-address 127.0.0.1 --receive-timeout 4000 \
    -P pcredit --add-pcr 100000000 --random -P continuity --fix \
    -O ip $BAD:$BAD_PORT --local-address 127.0.0.1 --ttl 1 >"$WORK/tsp_relay.log" 2>&1 &
RELAYPID=$!

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 120 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$RAW:$RAW_PORT?localaddr=127.0.0.1&ttl=1" 2>"$WORK/ffmpeg.log" &
FFPID=$!

wait_until $CAS_UP_S cas_settled
log_has "$tvlog" "channel+stream established" || { dump_logs; fail "regenerate cas: ECMG channel not established"; }
sleep $SCRAMBLE_WINDOW_S

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null || true
wait $RELAYPID 2>/dev/null || true
kill -INT $TVPID 2>/dev/null
wait $TVPID 2>/dev/null || true
wait $TSPID 2>/dev/null || true

[ -s "$cap" ] || { dump_logs; fail "regenerate cas: no packets captured"; }

verify=$(tsp -I file "$cap" -P pcrverify --pid 0x100 -O drop 2>&1 | grep "PCR OK" | sed 's/\([0-9]\),\([0-9]\)/\1\2/g')
ok=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\1/p')
bad=$(echo "$verify" | sed -n 's/.*: \([0-9][0-9]*\) PCR OK, \([0-9][0-9]*\) with jitter.*/\2/p')
[ "${ok:-0}" -ge 100 ] || fail "regenerate cas: only ${ok:-0} verified PCRs ($verify)"
[ "${bad:-1}" = "0" ] || fail "regenerate cas: $bad PCRs with jitter above 1 ms ($verify)"

missing=$(tsp -I file "$cap" -P continuity -O drop 2>&1 | grep -c "missing")
[ "$missing" = "0" ] || fail "regenerate cas: $missing continuity errors"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "regenerate cas: tsanalyze failed, see $WORK/tsanalyze.log"
scrambled=$(jq -r '.services[0].components.scrambled' "$report")
[ "${scrambled:-0}" -ge 2 ] || { dump_logs; fail "regenerate cas: expected >=2 scrambled components, got $scrambled"; }
bitrate=$(jq '.ts["pcr-bitrate"]' "$report")
[ "${bitrate:-0}" -ge 2970000 ] && [ "${bitrate:-0}" -le 3030000 ] \
    || fail "regenerate cas: PCR-derived bitrate $bitrate, expected 3000000 within 1%"

echo "OK"

#EOF
