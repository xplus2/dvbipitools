#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v curl >/dev/null 2>&1 || fail "required tool 'curl' not found on PATH"

DIPIBCG=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipibcg/dipibcg\1#')
[ -x "$DIPIBCG" ] || DIPIBCG="./dipibcg"
[ -x "$DIPIBCG" ] || fail "cannot locate dipibcg binary (tried $DIPIBCG)"

MCAST=239.255.9.11
PORT=17911
HTTPPORT=19192
SOCK="$WORK/metrics.sock"

metric_value() {
    grep -F "$2" "$1" | tail -1 | awk '{print $NF}'
}

# minute-aligned so the xmltv string round-trips exactly through dipibcg's
# own MJD-minutes representation (which has no seconds resolution)
now_epoch=$(date -u +%s)
start_epoch=$(((now_epoch + 1800) / 60 * 60))
stop_epoch=$(((now_epoch + 5400) / 60 * 60))
start_str=$(date -u -d "@$start_epoch" +%Y%m%d%H%M%S)
stop_str=$(date -u -d "@$stop_epoch" +%Y%m%d%H%M%S)

cat >"$WORK/guide.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="ch1"><display-name>Channel One</display-name></channel>
  <programme start="$start_str +0000" stop="$stop_str +0000" channel="ch1">
    <title>Integration Test Show</title>
  </programme>
</tv>
EOF
cat >"$WORK/map.csv" <<EOF
ch1,udp://239.1.9.3:5000,1,1,101
EOF

timeout 8 "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
sleep 0.3

timeout 6 "$DIPIBCG" -a -i "$WORK/guide.xml" -M "$WORK/map.csv" -w 24 \
    -m $MCAST:$PORT --metrics-id bcg-it --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipibcg.log" 2>&1 &
BPID=$!

sleep 2

body="$WORK/metrics.txt"
code=$(curl -s -o "$body" -w "%{http_code}" "http://127.0.0.1:$HTTPPORT/metrics")
[ "$code" = "200" ] || fail "GET /metrics: expected HTTP 200, got $code"

assert_contains "$body" 'dvbipi_bcg_sources_configured{component="bcg",instance="bcg-it"} 1' "sources_configured value"
assert_contains "$body" 'dvbipi_bcg_sources_up{component="bcg",instance="bcg-it"} 1' "sources_up value"
assert_contains "$body" 'dvbipi_bcg_services{component="bcg",instance="bcg-it"} 1' "services value"
assert_contains "$body" 'dvbipi_bcg_services_with_events{component="bcg",instance="bcg-it"} 1' "services_with_events value"
assert_contains "$body" 'dvbipi_bcg_events{component="bcg",instance="bcg-it"} 1' "events value"
assert_contains "$body" "dvbipi_bcg_schedule_start_time_seconds{component=\"bcg\",instance=\"bcg-it\"} $start_epoch" "schedule_start_time_seconds value"
assert_contains "$body" "dvbipi_bcg_schedule_end_time_seconds{component=\"bcg\",instance=\"bcg-it\"} $stop_epoch" "schedule_end_time_seconds value"

publications=$(metric_value "$body" 'dvbipi_bcg_publications_total{component="bcg",instance="bcg-it"}')
[ "${publications:-0}" -ge 1 ] || fail "expected at least one published document, got '$publications'"
doc_errors=$(metric_value "$body" 'dvbipi_bcg_document_errors_total{component="bcg",instance="bcg-it"}')
[ "${doc_errors:-1}" = "0" ] || fail "expected zero document errors, got '$doc_errors'"

kill $BPID 2>/dev/null
kill $MPID 2>/dev/null
wait $BPID 2>/dev/null
wait $MPID 2>/dev/null

echo "OK"

#EOF
