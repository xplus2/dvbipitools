#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg tsp tsanalyze jq ss; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
require_itest_helper

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 3)
PORT=$((FPB + 0))
HTTP_PORT1=$((FPB + 1))
HTTP_PORT2=$((FPB + 2))

ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=3" \
    -c:a libmp3lame -f mp3 "$WORK/stream1.mp3"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=2000:duration=3" \
    -c:a libmp3lame -f mp3 "$WORK/stream2.mp3"

mkdir -p "$WORK/httproot1" "$WORK/httproot2"
cp "$WORK/stream1.mp3" "$WORK/httproot1/stream.mp3"
cp "$WORK/stream2.mp3" "$WORK/httproot2/stream.mp3"

cap="$WORK/mpts_capture.ts"
report="$WORK/mpts_report.json"

"$DVBIPI_ITEST_HELPER" httpd "$HTTP_PORT1" "$WORK/httproot1" >"$WORK/httpd1.log" 2>&1 &
HTTPD1=$!
"$DVBIPI_ITEST_HELPER" httpd "$HTTP_PORT2" "$WORK/httproot2" >"$WORK/httpd2.log" 2>&1 &
HTTPD2=$!
trap 'kill $HTTPD1 $HTTPD2 2>/dev/null; rm -rf "$WORK"' EXIT
i=0
while ! grep -q "Serving HTTP" "$WORK/httpd1.log" 2>/dev/null || \
      ! grep -q "Serving HTTP" "$WORK/httpd2.log" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 200 ] || fail "mpts: http server(s) never became ready"
    sleep 0.1
done

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 60000 \
    -O file "$cap" >"$WORK/tsp_mpts.log" 2>&1 &
TSPID=$!
wait_until 30 udp_port_busy $PORT || fail "tsp capture never bound $PORT"

DEADLINE_S=${DEADLINE_S:-60}

mpts_ready() {
    [ -s "$cap" ] || return 1
    tsanalyze --json "$cap" >"$report" 2>/dev/null || return 1
    jq -e '[.services[]? | select(.name == "Station One" or .name == "Station Two")] | length >= 2' \
        "$report" >/dev/null 2>&1
}

timeout $((DEADLINE_S + 10)) "$BIN" -O lo -m $MCAST:$PORT \
    -i "http://127.0.0.1:$HTTP_PORT1/stream.mp3" --sid 101 -s "Station One" \
    -i "http://127.0.0.1:$HTTP_PORT2/stream.mp3" --sid 102 -s "Station Two" >"$WORK/dipiradiohead.log" 2>&1 &
RDPID=$!

wait_until $DEADLINE_S mpts_ready || :

kill $RDPID $TSPID 2>/dev/null
wait $RDPID $TSPID 2>/dev/null || true
kill $HTTPD1 $HTTPD2 2>/dev/null || true

[ -s "$cap" ] || fail "mpts: no packets captured"

tsanalyze --json "$cap" > "$report" 2>"$WORK/tsanalyze_mpts.log" || fail "mpts: tsanalyze failed, see $WORK/tsanalyze_mpts.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "mpts: $cc_errors continuity-counter errors in capture"

services=$(jq '.services | length' "$report")
[ "${services:-0}" -ge 2 ] || fail "mpts: expected 2 services, got $services"

for name in "Station One" "Station Two"; do
    match=$(jq --arg n "$name" '[.services[] | select(.name == $n)] | length' "$report")
    [ "${match:-0}" -ge 1 ] || fail "mpts: expected service named '$name' not found"
done

echo "OK"

#EOF
