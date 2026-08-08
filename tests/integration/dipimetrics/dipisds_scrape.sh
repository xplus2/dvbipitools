#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v curl >/dev/null 2>&1 || fail "required tool 'curl' not found on PATH"

DIPISDS=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipisds/dipisds\1#')
[ -x "$DIPISDS" ] || DIPISDS="./dipisds"
[ -x "$DIPISDS" ] || fail "cannot locate dipisds binary (tried $DIPISDS)"

MCAST=239.255.9.10
PORT=17910
HTTPPORT=19191
SOCK="$WORK/metrics.sock"

metric_value() {
    grep -F "$2" "$1" | tail -1 | awk '{print $NF}'
}

cat >"$WORK/channels.csv" <<EOF
svc1,udp://239.1.9.1:5000
svc2,udp://239.1.9.2:5000
EOF

timeout 8 "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
sleep 0.3

timeout 6 "$DIPISDS" -a -i "$WORK/channels.csv" -p example.org -O "Test Headend" \
    -m $MCAST:$PORT --metrics-id sds-it --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipisds.log" 2>&1 &
SPID=$!

sleep 2

body="$WORK/metrics.txt"
code=$(curl -s -o "$body" -w "%{http_code}" "http://127.0.0.1:$HTTPPORT/metrics")
[ "$code" = "200" ] || fail "GET /metrics: expected HTTP 200, got $code"

assert_contains "$body" 'dvbipi_headend_info{component="sds",instance="sds-it",version="' "headend_info present"
assert_contains "$body" 'dvbipi_sds_service_providers{component="sds",instance="sds-it"} 1' "sds_service_providers value"
assert_contains "$body" 'dvbipi_sds_services{component="sds",instance="sds-it"} 2' "sds_services value"
assert_contains "$body" 'dvbipi_sds_announcements_total{component="sds",instance="sds-it",transport="multicast"}' "announcements_total present"
assert_contains "$body" 'dvbipi_metrics_snapshot_age_seconds{component="sds",instance="sds-it"}' "snapshot_age present"
assert_contains "$body" '# EOF' "EOF terminator"

announcements=$(metric_value "$body" 'dvbipi_sds_announcements_total{component="sds",instance="sds-it",transport="multicast"}')
[ "${announcements:-0}" -ge 1 ] || fail "expected at least one completed announcement cycle, got '$announcements'"

code404=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$HTTPPORT/nope")
[ "$code404" = "404" ] || fail "GET /nope: expected HTTP 404, got $code404"

kill $SPID 2>/dev/null
kill $MPID 2>/dev/null
wait $SPID 2>/dev/null
wait $MPID 2>/dev/null

echo "OK"

#EOF
