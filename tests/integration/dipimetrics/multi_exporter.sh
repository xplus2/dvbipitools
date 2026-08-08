#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v curl >/dev/null 2>&1 || fail "required tool 'curl' not found on PATH"

DIPISDS=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipisds/dipisds\1#')
[ -x "$DIPISDS" ] || DIPISDS="./dipisds"
[ -x "$DIPISDS" ] || fail "cannot locate dipisds binary (tried $DIPISDS)"

DIPIBCG=$(echo "$BIN" | sed 's#/dipimetrics\([^/]*\)$#/../dipibcg/dipibcg\1#')
[ -x "$DIPIBCG" ] || DIPIBCG="./dipibcg"
[ -x "$DIPIBCG" ] || fail "cannot locate dipibcg binary (tried $DIPIBCG)"

SDS_MCAST=239.255.9.12
SDS_PORT=17912
BCG_MCAST=239.255.9.13
BCG_PORT=17913
HTTPPORT=19193
SOCK="$WORK/metrics.sock"

now_epoch=$(date -u +%s)
start_epoch=$(((now_epoch + 1800) / 60 * 60))
stop_epoch=$(((now_epoch + 5400) / 60 * 60))
start_str=$(date -u -d "@$start_epoch" +%Y%m%d%H%M%S)
stop_str=$(date -u -d "@$stop_epoch" +%Y%m%d%H%M%S)

cat >"$WORK/channels.csv" <<EOF
svc1,udp://239.1.9.4:5000
EOF
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
ch1,udp://239.1.9.5:5000,1,1,101
EOF

timeout 8 "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
sleep 0.3

timeout 6 "$DIPISDS" -a -i "$WORK/channels.csv" -p example.org -O "Test Headend" \
    -m $SDS_MCAST:$SDS_PORT --metrics-id sds-multi --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipisds.log" 2>&1 &
SPID=$!

timeout 6 "$DIPIBCG" -a -i "$WORK/guide.xml" -M "$WORK/map.csv" -w 24 \
    -m $BCG_MCAST:$BCG_PORT --metrics-id bcg-multi --metrics "$SOCK" --metrics-interval 1 \
    >"$WORK/dipibcg.log" 2>&1 &
BPID=$!

sleep 2

body="$WORK/metrics.txt"
code=$(curl -s -o "$body" -w "%{http_code}" "http://127.0.0.1:$HTTPPORT/metrics")
[ "$code" = "200" ] || fail "GET /metrics: expected HTTP 200, got $code"

assert_contains "$body" 'dvbipi_headend_info{component="sds",instance="sds-multi",version="' "sds headend_info present"
assert_contains "$body" 'dvbipi_headend_info{component="bcg",instance="bcg-multi",version="' "bcg headend_info present"
assert_contains "$body" 'dvbipi_sds_services{component="sds",instance="sds-multi"} 1' "sds services value"
assert_contains "$body" 'dvbipi_bcg_services{component="bcg",instance="bcg-multi"} 1' "bcg services value"

# the same family must group both instances' samples together, not interleave
# with other families - one TYPE/HELP pair for dvbipi_headend_info total
info_type_count=$(grep -c '^# TYPE dvbipi_headend_info ' "$body")
[ "$info_type_count" = "1" ] || fail "expected exactly one dvbipi_headend_info TYPE line, got $info_type_count"

age_count=$(grep -c '^dvbipi_metrics_snapshot_age_seconds{' "$body")
[ "$age_count" = "2" ] || fail "expected snapshot_age_seconds for both instances, got $age_count series"

kill $SPID 2>/dev/null
kill $BPID 2>/dev/null
kill $MPID 2>/dev/null
wait $SPID 2>/dev/null
wait $BPID 2>/dev/null
wait $MPID 2>/dev/null

echo "OK"

#EOF
