#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze tstables jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 1)
PORT=$((FPB + 0))

clip="$WORK/clip.ts"
cap="$WORK/ait_capture.ts"
report="$WORK/ait_report.json"
ait="$WORK/ait_tables.txt"

gen_test_clip "$clip" 1000 3

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 4000 \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!
sleep 0.3

"$BIN" -O lo -u -m $MCAST:$PORT -i - -s "Ait Channel" \
    --hbbtv http://example.invalid/app.html --hbbtv-org-id 1 --hbbtv-app-id 2 <"$clip" >"$WORK/dipitvhead.log" 2>&1

wait $TSPID || true

[ -s "$cap" ] || fail "hbbtv: no packets captured (see $WORK/dipitvhead.log)"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" \
    || fail "hbbtv: tsanalyze failed, see $WORK/tsanalyze.log"

ait_packets=$(jq '[.pids[]? | select(.description | test("AIT")) | .packets.total] | add // 0' "$report")
[ "${ait_packets:-0}" -ge 1 ] || fail "hbbtv: no AIT PID carrying packets in capture"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "hbbtv: $cc_errors continuity-counter errors in capture"

ait_pid=$(jq -r '[.pids[]? | select(.description | test("AIT")) | .id][0]' "$report")
tstables --pid "$ait_pid" --tid 0x74 --all-once "$cap" >"$ait" 2>&1
assert_contains "$ait" "Organization id: 0x00000001" "hbbtv organisation id"
assert_contains "$ait" "Application id: 0x0002" "hbbtv application id"
assert_contains "$ait" 'URL base: "http://example.invalid/app.html"' "hbbtv url"

echo "OK"

#EOF
