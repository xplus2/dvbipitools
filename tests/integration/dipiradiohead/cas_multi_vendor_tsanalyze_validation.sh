#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.45
ECMG_A_PORT=12246
ECMG_B_PORT=12247
EMMG_A_PORT=18004
EMMG_B_PORT=18005
HTTP_PORT=18083

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=12" \
    -c:a libmp3lame -f mp3 "$WORK/stream.mp3"

# a plain http.server serving a static file delivers it near-instantly (no live-source
# pacing), which races dipiradiohead's content-time-based CAS clock through the whole
# test in well under a real second and starves the ECMG worker threads of wall-clock
# time to complete their (network-latency-bound) round trips - the source must pace
# delivery like a real icecast/shoutcast station does. ffmpeg -re -listen paces
# correctly and needs no external script now that dipiradiohead's http client
# dechunks Transfer-Encoding: chunked (which -listen's http server always uses).
start_http_source() {
    ffmpeg -hide_banner -loglevel error -re -i "$WORK/stream.mp3" -c copy -f mp3 \
        -listen 1 "http://127.0.0.1:$HTTP_PORT/stream.mp3" \
        >"$WORK/ratesrv_$1.log" 2>&1 &
    FFSERVE_PID=$!
    i=0
    while ! ss -ltn 2>/dev/null | awk '{print $4}' | grep -q ":$HTTP_PORT\$"; do
        i=$((i + 1))
        [ "$i" -lt 200 ] || fail "multi-cas: paced http source on $HTTP_PORT never became ready"
        sleep 0.1
    done
}

wait_for_port() {
    port=$1
    what=$2
    i=0
    while ! ss -ltn 2>/dev/null | awk '{print $4}' | grep -q ":$port\$"; do
        i=$((i + 1))
        [ "$i" -lt 100 ] || fail "multi-cas: $what on $port never became ready"
        sleep 0.05
    done
}

# phase 1: both vendors up - content scrambled, both CA_descriptors present with the right
# CA_system_id on the right pid (super_cas_id >> 16: 0x4A750002 -> 19061, 0x0D960001 -> 3478)
PORT1=17745
cap1="$WORK/cas_multi_steady.ts"
report1="$WORK/cas_multi_steady.json"

tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a1.log" 2>&1 &
ECMG_A_PID=$!
tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b1.log" 2>&1 &
ECMG_B_PID=$!
wait_for_port $ECMG_A_PORT "tsecmg vendor A"
wait_for_port $ECMG_B_PORT "tsecmg vendor B"
start_http_source 1

tsp -I ip $MCAST:$PORT1 --local-address 127.0.0.1 --receive-timeout 15000 \
    -O file "$cap1" >"$WORK/tsp1.log" 2>&1 &
TSPID=$!
sleep 0.3

timeout 20 "$BIN" -I lo -m $MCAST:$PORT1 -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "Multi CAS Steady" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_A_PORT" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_B_PORT" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipiradiohead1.log" 2>&1 || true

wait $TSPID || true
kill $ECMG_A_PID $ECMG_B_PID $FFSERVE_PID 2>/dev/null

[ -s "$cap1" ] || fail "multi-cas steady: no packets captured (see $WORK/dipiradiohead1.log)"

tsanalyze --json "$cap1" > "$report1" 2>"$WORK/tsanalyze1.log" \
    || fail "multi-cas steady: tsanalyze failed, see $WORK/tsanalyze1.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report1")
[ "$is_scrambled" = "true" ] || fail "multi-cas steady: expected scrambled output, is-scrambled=$is_scrambled"

ecm_a_cas=$(jq -r '.pids[] | select(.id==32) | .cas' "$report1")
[ "$ecm_a_cas" = "19061" ] || fail "multi-cas steady: expected vendor A's CA_descriptor (cas=19061) on pid 0x0020, got '$ecm_a_cas'"

ecm_b_cas=$(jq -r '.pids[] | select(.id==34) | .cas' "$report1")
[ "$ecm_b_cas" = "3478" ] || fail "multi-cas steady: expected vendor B's CA_descriptor (cas=3478) on pid 0x0022, got '$ecm_b_cas'"

# phase 2: non-required vendor B's ECMG is unreachable throughout - content must stay scrambled
PORT2=17746
cap2="$WORK/cas_multi_nonrequired_down.ts"
report2="$WORK/cas_multi_nonrequired_down.json"

tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a2.log" 2>&1 &
ECMG_A_PID=$!
wait_for_port $ECMG_A_PORT "tsecmg vendor A"
start_http_source 2

tsp -I ip $MCAST:$PORT2 --local-address 127.0.0.1 --receive-timeout 15000 \
    -O file "$cap2" >"$WORK/tsp2.log" 2>&1 &
TSPID=$!
sleep 0.3

timeout 20 "$BIN" -I lo -m $MCAST:$PORT2 -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "Multi CAS Nonrequired Down" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_A_PORT" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:19999" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipiradiohead2.log" 2>&1 || true

wait $TSPID || true
kill $ECMG_A_PID $FFSERVE_PID 2>/dev/null

[ -s "$cap2" ] || fail "multi-cas nonrequired-down: no packets captured (see $WORK/dipiradiohead2.log)"

tsanalyze --json "$cap2" > "$report2" 2>"$WORK/tsanalyze2.log" \
    || fail "multi-cas nonrequired-down: tsanalyze failed, see $WORK/tsanalyze2.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report2")
[ "$is_scrambled" = "true" ] || fail "multi-cas nonrequired-down: expected content to stay scrambled with only a non-required vendor down, is-scrambled=$is_scrambled"

# phase 3: required vendor A's ECMG is unreachable throughout, --cas-fallback-clear set -
# content must go clear even though non-required vendor B is healthy
PORT3=17747
cap3="$WORK/cas_multi_required_down.ts"
report3="$WORK/cas_multi_required_down.json"

tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b3.log" 2>&1 &
ECMG_B_PID=$!
wait_for_port $ECMG_B_PORT "tsecmg vendor B"
start_http_source 3

tsp -I ip $MCAST:$PORT3 --local-address 127.0.0.1 --receive-timeout 15000 \
    -O file "$cap3" >"$WORK/tsp3.log" 2>&1 &
TSPID=$!
sleep 0.3

timeout 20 "$BIN" -I lo -m $MCAST:$PORT3 -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "Multi CAS Required Down" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:19998" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_B_PORT" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipiradiohead3.log" 2>&1 || true

wait $TSPID || true
kill $ECMG_B_PID $FFSERVE_PID 2>/dev/null

[ -s "$cap3" ] || fail "multi-cas required-down: no packets captured (see $WORK/dipiradiohead3.log)"

tsanalyze --json "$cap3" > "$report3" 2>"$WORK/tsanalyze3.log" \
    || fail "multi-cas required-down: tsanalyze failed, see $WORK/tsanalyze3.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report3")
[ "$is_scrambled" = "false" ] || fail "multi-cas required-down: expected clear output with the required vendor down and --cas-fallback-clear, is-scrambled=$is_scrambled"

echo "OK"

#EOF
