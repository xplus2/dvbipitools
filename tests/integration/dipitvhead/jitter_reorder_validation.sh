#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
require_itest_helper

IN_GROUP=$(unique_mcast 61)
FPB=$(free_port_block 2)
IN_PORT=$((FPB + 0))
OUT_GROUP=$(unique_mcast 62)
OUT_PORT=$((FPB + 1))

clip="$WORK/clip.ts"
cap="$WORK/capture.ts"
report="$WORK/report.json"
rxlog="$WORK/receiver.log"

gen_test_clip "$clip" 1000 3

tsp -I ip "$OUT_GROUP:$OUT_PORT" --local-address 127.0.0.1 --receive-timeout 60000 -O file "$cap" >"$WORK/tsp.log" 2>&1 &
tsp_pid=$!
wait_until 30 udp_port_busy $OUT_PORT || fail "tsp capture never bound $OUT_PORT"

"$BIN" -i "rtp://@$IN_GROUP:$IN_PORT" -I lo --jitter-ms 100 -O lo -u -m "$OUT_GROUP:$OUT_PORT" -s "Jitter Channel" >"$rxlog" 2>&1 &
rx_pid=$!
wait_until 30 udp_port_busy $IN_PORT || fail "receiver never bound $IN_PORT (see $rxlog)"
kill -0 "$rx_pid" 2>/dev/null || fail "receiver exited early (see $rxlog)"

"$DVBIPI_ITEST_HELPER" rtp-send "$clip" "$IN_GROUP" "$IN_PORT" || fail "sender failed"
sleep 1.5
kill -INT "$rx_pid" 2>/dev/null
wait "$rx_pid" 2>/dev/null
sleep 0.5
kill "$tsp_pid" 2>/dev/null
wait "$tsp_pid" 2>/dev/null || true

[ -s "$cap" ] || fail "no packets captured (see $rxlog)"

assert_contains "$rxlog" "jitter buffer: reordered [1-9]" "reordering seen"
assert_contains "$rxlog" "lost 0 late 0 dup 0" "nothing lost or late"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "$cc_errors continuity-counter errors in capture"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "Jitter Channel" ] || fail "expected service name 'Jitter Channel', got '$service_name'"

echo "OK"
