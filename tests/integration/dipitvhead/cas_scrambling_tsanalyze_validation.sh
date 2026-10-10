#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

DEADLINE_S=${DEADLINE_S:-60}
MCAST=$(unique_mcast 61)
FPB=$(free_port_block 3)
PORT=$((FPB + 0))
ECMG_PORT=$((FPB + 1))
EMMG_PORT=$((FPB + 2))

cap="$WORK/cas_capture.ts"
report="$WORK/cas_report.json"

gen_test_clip "$WORK/clip.ts" 1000 $((DEADLINE_S + 10))

port_listening() {
    ss -H -ltn "sport = :$1" 2>/dev/null | grep -q .
}

scrambled_ready() {
    [ -s "$cap" ] || return 1
    tsanalyze --json "$cap" >"$report" 2>/dev/null || return 1
    jq -e '
        (.services[0]["is-scrambled"] | tostring) == "true"
        and ([.pids[]? | select(.video == true or .audio == true) | .pes // 0] | add // 0) >= 10
        and (.services[0].components.scrambled // 0) >= 2
    ' "$report" >/dev/null 2>&1
}

tsecmg -p $ECMG_PORT -s --log-protocol=info >"$WORK/tsecmg.log" 2>&1 &
ECMGPID=$!
wait_until 30 port_listening $ECMG_PORT || fail "cas: tsecmg on $ECMG_PORT never became ready"

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout $((DEADLINE_S * 1000 + 15000)) \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!

mkfifo "$WORK/in.ts"
ffmpeg -hide_banner -loglevel error -re -i "$WORK/clip.ts" -c copy -f mpegts - \
    >"$WORK/in.ts" 2>"$WORK/ffmpeg.log" &
FFPID=$!

timeout $((DEADLINE_S + 10)) "$BIN" -O lo -u -m $MCAST:$PORT -i - -s "CAS Test" \
    --cas-algo cissa --cas-ecmg "tcp://127.0.0.1:$ECMG_PORT" --cas-ecmg-version 2 \
    --cas-emmg-port $EMMG_PORT --cas-super-id 0x4A750002 --cas-ecm-id 1 --cas-pids video,audio \
    --cas-cp-duration 3000 \
    <"$WORK/in.ts" >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!

end=$(( $(date +%s) + DEADLINE_S ))
until scrambled_ready; do
    [ "$(date +%s)" -lt "$end" ] || break
    sleep 0.5
done

kill $TVPID $TSPID $FFPID $ECMGPID 2>/dev/null
wait $TVPID $TSPID $FFPID 2>/dev/null || true

[ -s "$cap" ] || fail "cas: no packets captured (see $WORK/dipitvhead.log)"
[ -s "$report" ] || fail "cas: tsanalyze produced no report from the capture"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$report")
[ "$is_scrambled" = "true" ] || fail "cas: expected scrambled output, is-scrambled=$is_scrambled"

scrambled_count=$(jq -r '.services[0].components.scrambled' "$report")
[ "${scrambled_count:-0}" -ge 2 ] || fail "cas: expected >=2 scrambled components (video+audio), got $scrambled_count"

echo "OK"

#EOF
