#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.7.44
ECMG_A_PORT=12244
ECMG_B_PORT=12245
EMMG_A_PORT=18002
EMMG_B_PORT=18003

# phase 1: both vendors up - content scrambled, both CA_descriptors present with the right
# CA_system_id on the right pid (super_cas_id >> 16: 0x4A750002 -> 19061, 0x0D960001 -> 3478)
PORT1=17744
cap1="$WORK/cas_multi_steady.ts"
report1="$WORK/cas_multi_steady.json"

tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a1.log" 2>&1 &
ECMG_A_PID=$!
tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b1.log" 2>&1 &
ECMG_B_PID=$!
sleep 0.3

tsp -I ip $MCAST:$PORT1 --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap1" >"$WORK/tsp1.log" 2>&1 &
TSPID=$!

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 10 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg1.log" | \
timeout 12 "$BIN" -O lo -u -m $MCAST:$PORT1 -i - -s "Multi CAS Steady" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_A_PORT" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_B_PORT" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-pids video,audio --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipitvhead1.log" 2>&1

wait $TSPID || true
kill $ECMG_A_PID $ECMG_B_PID 2>/dev/null

[ -s "$cap1" ] || fail "multi-cas steady: no packets captured (see $WORK/dipitvhead1.log)"

tsanalyze --json "$cap1" > "$report1" 2>"$WORK/tsanalyze1.log" \
    || fail "multi-cas steady: tsanalyze failed, see $WORK/tsanalyze1.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report1")
[ "$is_scrambled" = "true" ] || fail "multi-cas steady: expected scrambled output, is-scrambled=$is_scrambled"

scrambled_count=$(jq -r '.services[0].components.scrambled' "$report1")
[ "${scrambled_count:-0}" -ge 2 ] || fail "multi-cas steady: expected >=2 scrambled components (video+audio), got $scrambled_count"

ecm_a_cas=$(jq -r '.pids[] | select(.id==32) | .cas' "$report1")
[ "$ecm_a_cas" = "19061" ] || fail "multi-cas steady: expected vendor A's CA_descriptor (cas=19061) on pid 0x0020, got '$ecm_a_cas'"

ecm_b_cas=$(jq -r '.pids[] | select(.id==34) | .cas' "$report1")
[ "$ecm_b_cas" = "3478" ] || fail "multi-cas steady: expected vendor B's CA_descriptor (cas=3478) on pid 0x0022, got '$ecm_b_cas'"

# phase 2: non-required vendor B's ECMG is unreachable throughout - content must stay scrambled
PORT2=17745
cap2="$WORK/cas_multi_nonrequired_down.ts"
report2="$WORK/cas_multi_nonrequired_down.json"

tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a2.log" 2>&1 &
ECMG_A_PID=$!
sleep 0.3

tsp -I ip $MCAST:$PORT2 --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap2" >"$WORK/tsp2.log" 2>&1 &
TSPID=$!

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 10 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg2.log" | \
timeout 12 "$BIN" -O lo -u -m $MCAST:$PORT2 -i - -s "Multi CAS Nonrequired Down" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_A_PORT" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:19999" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-pids video,audio --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipitvhead2.log" 2>&1

wait $TSPID || true
kill $ECMG_A_PID 2>/dev/null

[ -s "$cap2" ] || fail "multi-cas nonrequired-down: no packets captured (see $WORK/dipitvhead2.log)"

tsanalyze --json "$cap2" > "$report2" 2>"$WORK/tsanalyze2.log" \
    || fail "multi-cas nonrequired-down: tsanalyze failed, see $WORK/tsanalyze2.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report2")
[ "$is_scrambled" = "true" ] || fail "multi-cas nonrequired-down: expected content to stay scrambled with only a non-required vendor down, is-scrambled=$is_scrambled"

# phase 3: required vendor A's ECMG is unreachable throughout, --cas-fallback-clear set -
# content must go clear even though non-required vendor B is healthy
PORT3=17746
cap3="$WORK/cas_multi_required_down.ts"
report3="$WORK/cas_multi_required_down.json"

tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b3.log" 2>&1 &
ECMG_B_PID=$!
sleep 0.3

tsp -I ip $MCAST:$PORT3 --local-address 127.0.0.1 --receive-timeout 6000 \
    -O file "$cap3" >"$WORK/tsp3.log" 2>&1 &
TSPID=$!

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 10 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg3.log" | \
timeout 12 "$BIN" -O lo -u -m $MCAST:$PORT3 -i - -s "Multi CAS Required Down" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:19998" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_B_PORT" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
    --cas-pids video,audio --cas-cp-duration 3000 --cas-fallback-clear \
    >"$WORK/dipitvhead3.log" 2>&1

wait $TSPID || true
kill $ECMG_B_PID 2>/dev/null

[ -s "$cap3" ] || fail "multi-cas required-down: no packets captured (see $WORK/dipitvhead3.log)"

tsanalyze --json "$cap3" > "$report3" 2>"$WORK/tsanalyze3.log" \
    || fail "multi-cas required-down: tsanalyze failed, see $WORK/tsanalyze3.log"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report3")
[ "$is_scrambled" = "false" ] || fail "multi-cas required-down: expected clear output with the required vendor down and --cas-fallback-clear, is-scrambled=$is_scrambled"

echo "OK"

#EOF
