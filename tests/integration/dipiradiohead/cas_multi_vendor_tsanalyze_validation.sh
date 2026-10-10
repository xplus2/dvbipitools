#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tsecmg jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=$(unique_mcast 61)
PORT_BASE=$(free_port_block 44)

DEADLINE_S=${DEADLINE_S:-60}

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=$((DEADLINE_S + 10))" \
    -c:a libmp3lame -f mp3 "$WORK/stream.mp3"

start_http_source() {
    ffmpeg -hide_banner -loglevel error -re -i "$WORK/stream.mp3" -c copy -f mp3 \
        -listen 1 "http://127.0.0.1:$HTTP_PORT/stream.mp3" \
        >"$WORK/ratesrv_$1.log" 2>&1 &
    FFSERVE_PID=$!
    wait_for_port $HTTP_PORT "paced http source"
}

port_listening() {
    ss -H -ltn "sport = :$1" 2>/dev/null | grep -q .
}

wait_for_port() {
    port=$1
    what=$2
    wait_until 30 port_listening "$port" || fail "multi-cas: $what on $port never became ready"
    return 0
}

phase_ready() {
    jq -e --arg s "$2" --arg a "$3" --arg b "$4" '
        (.services[0]["is-scrambled"] | tostring) == $s
        and ([.pids[]? | select(.audio == true) | .pes // 0] | add // 0) >= 10
        and ($a == "-" or ([.pids[]? | select(.id == 32) | .cas | tostring] | first) == $a)
        and ($b == "-" or ([.pids[]? | select(.id == 34) | .cas | tostring] | first) == $b)
    ' "$1" >/dev/null 2>&1
}

poll_capture() {
    cap=$1
    report=$2
    shift 2
    [ -s "$cap" ] || return 1
    tsanalyze --json "$cap" >"$report" 2>/dev/null || return 1
    phase_ready "$report" "$@"
}

run_phase() {
    n=$1
    mport=$2
    name=$3
    a_up=$4
    b_up=$5
    want_scrambled=$6
    want_a=$7
    want_b=$8
    cap="$WORK/cas_phase$n.ts"
    report="$WORK/cas_phase$n.json"
    base=$((PORT_BASE + 10 * n))
    ECMG_A_PORT=$((base + 1))
    ECMG_B_PORT=$((base + 2))
    EMMG_A_PORT=$((base + 3))
    EMMG_B_PORT=$((base + 4))
    HTTP_PORT=$((base + 5))
    a_port=$((base + 6))
    b_port=$((base + 7))
    servers=""

    if [ "$a_up" = 1 ]; then
        tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a$n.log" 2>&1 &
        servers="$servers $!"
        a_port=$ECMG_A_PORT
        wait_for_port $ECMG_A_PORT "tsecmg vendor A"
    fi
    if [ "$b_up" = 1 ]; then
        tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b$n.log" 2>&1 &
        servers="$servers $!"
        b_port=$ECMG_B_PORT
        wait_for_port $ECMG_B_PORT "tsecmg vendor B"
    fi
    start_http_source $n

    tsp -I ip $MCAST:$mport --local-address 127.0.0.1 --receive-timeout $((DEADLINE_S * 1000 + 15000)) \
        -O file "$cap" >"$WORK/tsp$n.log" 2>&1 &
    TSPID=$!
    sleep 0.2

    timeout $((DEADLINE_S + 10)) "$BIN" -O lo -m $MCAST:$mport -i "http://127.0.0.1:$HTTP_PORT/stream.mp3" -s "$name" \
        --cas-algo cissa \
        --cas-ecmg "tcp://127.0.0.1:$a_port" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
                   --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-port $EMMG_A_PORT --cas-required \
        --cas-ecmg "tcp://127.0.0.1:$b_port" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
                   --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-port $EMMG_B_PORT \
        --cas-cp-duration 1000 --cas-fallback-clear \
        >"$WORK/dipiradiohead$n.log" 2>&1 &
    RDPID=$!

    end=$(( $(date +%s) + DEADLINE_S ))
    until poll_capture "$cap" "$report" "$want_scrambled" "$want_a" "$want_b"; do
        [ "$(date +%s)" -lt "$end" ] || break
        sleep 0.5
    done

    kill $RDPID $TSPID $servers $FFSERVE_PID 2>/dev/null
    wait $RDPID $TSPID $FFSERVE_PID 2>/dev/null || true

    [ -s "$cap" ] || fail "$name: no packets captured (see $WORK/dipiradiohead$n.log)"
    [ -s "$report" ] || fail "$name: tsanalyze produced no report from the capture"
    return 0
}

run_phase 1 $((PORT_BASE + 40 + 1)) "Multi CAS Steady" 1 1 true 19061 3478 &
PHASE1_PID=$!
run_phase 2 $((PORT_BASE + 40 + 2)) "Multi CAS Nonrequired Down" 1 0 true - - &
PHASE2_PID=$!
run_phase 3 $((PORT_BASE + 40 + 3)) "Multi CAS Required Down" 0 1 false - - &
PHASE3_PID=$!
wait $PHASE1_PID || fail "multi-cas steady: phase failed"
wait $PHASE2_PID || fail "multi-cas nonrequired-down: phase failed"
wait $PHASE3_PID || fail "multi-cas required-down: phase failed"

# both vendors up - content scrambled, both CA_descriptors present with the right
# CA_system_id on the right pid (super_cas_id >> 16: 0x4A750002 -> 19061, 0x0D960001 -> 3478)
is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase1.json")
[ "$is_scrambled" = "true" ] || fail "multi-cas steady: expected scrambled output, is-scrambled=$is_scrambled"

ecm_a_cas=$(jq -r '.pids[] | select(.id==32) | .cas' "$WORK/cas_phase1.json")
[ "$ecm_a_cas" = "19061" ] || fail "multi-cas steady: expected vendor A's CA_descriptor (cas=19061) on pid 0x0020, got '$ecm_a_cas'"

ecm_b_cas=$(jq -r '.pids[] | select(.id==34) | .cas' "$WORK/cas_phase1.json")
[ "$ecm_b_cas" = "3478" ] || fail "multi-cas steady: expected vendor B's CA_descriptor (cas=3478) on pid 0x0022, got '$ecm_b_cas'"

# non-required vendor B's ECMG is unreachable throughout - content must stay scrambled
is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase2.json")
[ "$is_scrambled" = "true" ] || fail "multi-cas nonrequired-down: expected content to stay scrambled with only a non-required vendor down, is-scrambled=$is_scrambled"

# required vendor A's ECMG is unreachable throughout, --cas-fallback-clear set -
# content must go clear even though non-required vendor B is healthy
is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase3.json")
[ "$is_scrambled" = "false" ] || fail "multi-cas required-down: expected clear output with the required vendor down and --cas-fallback-clear, is-scrambled=$is_scrambled"

echo "OK"

#EOF
