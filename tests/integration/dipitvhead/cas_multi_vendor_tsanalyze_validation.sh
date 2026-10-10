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
PORT_BASE=$(free_port_block 44)

gen_test_clip "$WORK/clip.ts" 1000 $((DEADLINE_S + 10))

port_listening() {
    ss -H -ltn "sport = :$1" 2>/dev/null | grep -q .
}

wait_for_port() {
    wait_until 30 port_listening "$1" || fail "multi-cas: $2 on $1 never became ready"
    return 0
}

phase_ready() {
    jq -e --arg s "$2" --arg c "$3" --arg a "$4" --arg b "$5" '
        (.services[0]["is-scrambled"] | tostring) == $s
        and ([.pids[]? | select(.video == true or .audio == true) | .pes // 0] | add // 0) >= 10
        and ($c == "-" or (.services[0].components.scrambled // 0) >= ($c | tonumber))
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
    want_comp=$7
    want_a=$8
    want_b=$9
    cap="$WORK/cas_phase$n.ts"
    report="$WORK/cas_phase$n.json"
    base=$((PORT_BASE + 10 * n))
    ECMG_A_PORT=$((base + 1))
    ECMG_B_PORT=$((base + 2))
    EMMG_A_PORT=$((base + 3))
    EMMG_B_PORT=$((base + 4))
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

    tsp -I ip $MCAST:$mport --local-address 127.0.0.1 --receive-timeout $((DEADLINE_S * 1000 + 15000)) \
        -O file "$cap" >"$WORK/tsp$n.log" 2>&1 &
    TSPID=$!

    mkfifo "$WORK/in$n.ts"
    exec 3<>"$WORK/in$n.ts"
    ffmpeg -hide_banner -loglevel error -progress "$WORK/progress$n.txt" -re -i "$WORK/clip.ts" \
        -c copy -f mpegts - >"$WORK/in$n.ts" 2>"$WORK/ffmpeg$n.log" &
    FFPID=$!
    wait_until 30 grep -q 'total_size=[1-9]' "$WORK/progress$n.txt" \
        || fail "$name: source never produced data (see $WORK/ffmpeg$n.log)"

    timeout $((DEADLINE_S + 10)) "$BIN" -O lo -u -m $MCAST:$mport -i - -s "$name" \
        --cas-algo cissa \
        --cas-ecmg "tcp://127.0.0.1:$a_port" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
                   --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-listen $EMMG_A_PORT --cas-required \
        --cas-ecmg "tcp://127.0.0.1:$b_port" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
                   --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-listen $EMMG_B_PORT \
        --cas-pids video,audio --cas-cp-duration 3000 --cas-fallback-clear \
        <"$WORK/in$n.ts" >"$WORK/dipitvhead$n.log" 2>&1 &
    TVPID=$!
    exec 3<&-

    end=$(( $(date +%s) + DEADLINE_S ))
    until poll_capture "$cap" "$report" "$want_scrambled" "$want_comp" "$want_a" "$want_b"; do
        [ "$(date +%s)" -lt "$end" ] || break
        sleep 0.5
    done

    kill $TVPID $TSPID $FFPID $servers 2>/dev/null
    wait $TVPID $TSPID $FFPID 2>/dev/null || true

    [ -s "$cap" ] || fail "$name: no packets captured (see $WORK/dipitvhead$n.log)"
    [ -s "$report" ] || fail "$name: tsanalyze produced no report from the capture"
    return 0
}

# phase 1: both vendors up - content scrambled, both CA_descriptors present with the right
# CA_system_id on the right pid (super_cas_id >> 16: 0x4A750002 -> 19061, 0x0D960001 -> 3478)
run_phase 1 $((PORT_BASE + 40 + 1)) "Multi CAS Steady" 1 1 true 2 19061 3478
# phase 2: non-required vendor B's ECMG is unreachable throughout - content must stay scrambled
run_phase 2 $((PORT_BASE + 40 + 2)) "Multi CAS Nonrequired Down" 1 0 true - - -
# phase 3: required vendor A's ECMG is unreachable throughout, --cas-fallback-clear set -
# content must go clear even though non-required vendor B is healthy
run_phase 3 $((PORT_BASE + 40 + 3)) "Multi CAS Required Down" 0 1 false - - -

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase1.json")
[ "$is_scrambled" = "true" ] || fail "multi-cas steady: expected scrambled output, is-scrambled=$is_scrambled"

scrambled_count=$(jq -r '.services[0].components.scrambled' "$WORK/cas_phase1.json")
[ "${scrambled_count:-0}" -ge 2 ] || fail "multi-cas steady: expected >=2 scrambled components (video+audio), got $scrambled_count"

ecm_a_cas=$(jq -r '.pids[] | select(.id==32) | .cas' "$WORK/cas_phase1.json")
[ "$ecm_a_cas" = "19061" ] || fail "multi-cas steady: expected vendor A's CA_descriptor (cas=19061) on pid 0x0020, got '$ecm_a_cas'"

ecm_b_cas=$(jq -r '.pids[] | select(.id==34) | .cas' "$WORK/cas_phase1.json")
[ "$ecm_b_cas" = "3478" ] || fail "multi-cas steady: expected vendor B's CA_descriptor (cas=3478) on pid 0x0022, got '$ecm_b_cas'"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase2.json")
[ "$is_scrambled" = "true" ] || fail "multi-cas nonrequired-down: expected content to stay scrambled with only a non-required vendor down, is-scrambled=$is_scrambled"

is_scrambled=$(jq -r '.services[0]["is-scrambled"]' "$WORK/cas_phase3.json")
[ "$is_scrambled" = "false" ] || fail "multi-cas required-down: expected clear output with the required vendor down and --cas-fallback-clear, is-scrambled=$is_scrambled"

echo "OK"

#EOF
