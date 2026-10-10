#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsecmg curl ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

DEADLINE_S=${DEADLINE_S:-60}

port_listening() {
    ss -H -ltn "sport = :$1" 2>/dev/null | grep -q .
}

wait_port() {
    wait_until 30 port_listening "$1"
}

DIPITVHEAD=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipitvhead/dipitvhead\1#')
[ -x "$DIPITVHEAD" ] || DIPITVHEAD="./dipitvhead"
[ -x "$DIPITVHEAD" ] || fail "cannot locate dipitvhead binary (tried $DIPITVHEAD)"

metric_value() {
    grep -F "$2" "$1" | tail -1 | awk '{print $NF}'
}

MCAST=$(unique_mcast 96)
PORT=$(free_udp_port)
TCP_BASE=$(free_tcp_port_block 5)
ECMG_A_PORT=$TCP_BASE
ECMG_B_PORT=$((TCP_BASE + 1))
EMMG_A_PORT=$((TCP_BASE + 2))
EMMG_B_PORT=$((TCP_BASE + 3))
HTTPPORT=$((TCP_BASE + 4))
SOCK="$WORK/metrics.sock"

# two CAS vendors, distinct super_cas_id (same values as
# dipitvhead/cas_multi_vendor_tsanalyze_validation.sh) - each must land its own counters
# under its own cas="0x..." label, not summed together, while scrambled/unexpected_clear
# (from the one shared scramble engine) stay a single unlabeled series.
tsecmg -p $ECMG_A_PORT -s --log-protocol=info >"$WORK/tsecmg_a.log" 2>&1 &
ECMG_A_PID=$!
tsecmg -p $ECMG_B_PORT -s --log-protocol=info >"$WORK/tsecmg_b.log" 2>&1 &
ECMG_B_PID=$!
wait_port $ECMG_A_PORT || fail "tsecmg (vendor A) never started listening on $ECMG_A_PORT (see $WORK/tsecmg_a.log)"
wait_port $ECMG_B_PORT || fail "tsecmg (vendor B) never started listening on $ECMG_B_PORT (see $WORK/tsecmg_b.log)"

timeout $((DEADLINE_S + 10)) "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
wait_port $HTTPPORT || fail "dipimetrics never started listening on $HTTPPORT (see $WORK/dipimetrics.log)"

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t $((DEADLINE_S + 10)) \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout $((DEADLINE_S + 10)) "$DIPITVHEAD" -O lo -u -m $MCAST:$PORT -i - -s "CAS Metrics Scrape" \
    --cas-algo cissa \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_A_PORT" --cas-ecmg-version 2 --cas-super-id 0x4A750002 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0020 --cas-emm-pid 0x0021 --cas-emmg-listen $EMMG_A_PORT --cas-required \
    --cas-ecmg "tcp://127.0.0.1:$ECMG_B_PORT" --cas-ecmg-version 2 --cas-super-id 0x0D960001 --cas-ecm-id 1 \
               --cas-ecm-pid 0x0022 --cas-emm-pid 0x0023 --cas-emmg-listen $EMMG_B_PORT \
    --cas-pids video,audio --cas-cp-duration 3000 \
    --metrics "$SOCK" --metrics-id tv-cas-it --metrics-interval 1 \
    >"$WORK/dipitvhead.log" 2>&1 &
TVPID=$!

body="$WORK/metrics.txt"

metrics_ready() {
    curl -sf -o "$body" "http://127.0.0.1:$HTTPPORT/metrics" || return 1
    log_has "$body" 'dvbipi_cas_ecmg_connected{component="tvhead",headend_id="tv-cas-it",cas="0x4a750002"} 1' || return 1
    log_has "$body" 'dvbipi_cas_ecmg_connected{component="tvhead",headend_id="tv-cas-it",cas="0x0d960001"} 1' || return 1
    mr_a=$(metric_value "$body" 'dvbipi_cas_ecm_total{component="tvhead",headend_id="tv-cas-it",cas="0x4a750002"}')
    mr_b=$(metric_value "$body" 'dvbipi_cas_ecm_total{component="tvhead",headend_id="tv-cas-it",cas="0x0d960001"}')
    [ "${mr_a:-0}" -ge 1 ] && [ "${mr_b:-0}" -ge 1 ]
}

end=$(( $(date +%s) + DEADLINE_S ))
until metrics_ready; do
    [ "$(date +%s)" -lt "$end" ] || break
    sleep 0.5
done
code=$(curl -s -o "$body" -w "%{http_code}" "http://127.0.0.1:$HTTPPORT/metrics")
[ "$code" = "200" ] || fail "GET /metrics: expected HTTP 200, got $code"

kill $TVPID $ECMG_A_PID $ECMG_B_PID $MPID 2>/dev/null
wait $TVPID $ECMG_A_PID $ECMG_B_PID $MPID 2>/dev/null

assert_contains "$body" 'dvbipi_cas_ecmg_connected{component="tvhead",headend_id="tv-cas-it",cas="0x4a750002"} 1' "vendor A ecmg_connected"
assert_contains "$body" 'dvbipi_cas_ecmg_connected{component="tvhead",headend_id="tv-cas-it",cas="0x0d960001"} 1' "vendor B ecmg_connected"

ecm_a=$(metric_value "$body" 'dvbipi_cas_ecm_total{component="tvhead",headend_id="tv-cas-it",cas="0x4a750002"}')
[ "${ecm_a:-0}" -ge 1 ] || fail "expected at least one ECM generated for vendor A, got '$ecm_a'"
ecm_b=$(metric_value "$body" 'dvbipi_cas_ecm_total{component="tvhead",headend_id="tv-cas-it",cas="0x0d960001"}')
[ "${ecm_b:-0}" -ge 1 ] || fail "expected at least one ECM generated for vendor B, got '$ecm_b'"

# shared scramble engine counters: one series, no per-vendor label
assert_contains "$body" 'dvbipi_cas_scrambled_packets_total{component="tvhead",headend_id="tv-cas-it"} ' "scrambled_packets_total has no cas label"
assert_not_contains "$body" 'dvbipi_cas_scrambled_packets_total{component="tvhead",headend_id="tv-cas-it",cas=' "scrambled_packets_total must not be split per-cas"

echo "OK"

#EOF
