#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v curl >/dev/null 2>&1 || fail "required tool 'curl' not found on PATH"

DIPISDS=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipisds/dipisds\1#')
[ -x "$DIPISDS" ] || DIPISDS="./dipisds"
[ -x "$DIPISDS" ] || fail "cannot locate dipisds binary (tried $DIPISDS)"

MCAST=239.255.9.14
PORT=17914
HTTPPORT=19194
SOCK="$WORK/metrics.sock"

metric_value() {
    grep -F "$2" "$1" | tail -1 | awk '{print $NF}'
}

cat >"$WORK/channels.csv" <<EOF
svc1,udp://239.1.9.6:5000
EOF

# short expiry so the "gone after silence" half of this test doesn't need a
# long sleep
timeout 20 "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" -e 2 -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
sleep 0.3

# run long enough to rack up several announce cycles before the restart
timeout 6 "$DIPISDS" -a -i "$WORK/channels.csv" -p example.org -O "Test Headend" \
    -m $MCAST:$PORT -t 1 --metrics-id restart-it --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipisds_1.log" 2>&1 &
S1PID=$!
sleep 4

body1="$WORK/metrics_before_restart.txt"
curl -s -o "$body1" "http://127.0.0.1:$HTTPPORT/metrics"
before=$(metric_value "$body1" 'dvbipi_sds_announcements_total{component="sds",instance="restart-it",transport="multicast"}')
[ "${before:-0}" -ge 2 ] || fail "expected several announcements before restart, got '$before'"

kill $S1PID 2>/dev/null
wait $S1PID 2>/dev/null

# restart immediately (well inside the 2s expiry window) with the same
# --metrics-id but a fresh process - a new process_start_time, sequence restarting at 1
timeout 3 "$DIPISDS" -a -i "$WORK/channels.csv" -p example.org -O "Test Headend" \
    -m $MCAST:$PORT -t 1 --metrics-id restart-it --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipisds_2.log" 2>&1 &
S2PID=$!
sleep 1.5

body2="$WORK/metrics_after_restart.txt"
curl -s -o "$body2" "http://127.0.0.1:$HTTPPORT/metrics"
after=$(metric_value "$body2" 'dvbipi_sds_announcements_total{component="sds",instance="restart-it",transport="multicast"}')
[ "${after:-0}" -lt "$before" ] || fail "restart should reset the counter (was $before, now $after) - looks like the stale-sequence datagram got rejected instead of accepted as a restart"

kill $S2PID 2>/dev/null
wait $S2PID 2>/dev/null

# now let the (already-stopped) exporter's silence exceed --expiry and
# confirm the instance drops out of /metrics entirely
sleep 3

body3="$WORK/metrics_after_expiry.txt"
curl -s -o "$body3" "http://127.0.0.1:$HTTPPORT/metrics"
assert_not_contains "$body3" 'instance="restart-it"' "instance still present after expiry"

kill $MPID 2>/dev/null
wait $MPID 2>/dev/null

echo "OK"

#EOF
