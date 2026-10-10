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
FPB=$(free_port_block 2)
PORT=$((FPB + 1))
HTTP_PORT=$((FPB + 0))

mkdir -p "$WORK/httproot"
ffmpeg -hide_banner -loglevel error -f lavfi -i "sine=frequency=1000:duration=3" \
    -c:a aac -f adts "$WORK/httproot/stream.aac"
[ -s "$WORK/httproot/stream.aac" ] || fail "adts: ffmpeg produced no AAC stream"

cap="$WORK/adts_capture.ts"
report="$WORK/adts_report.json"

"$DVBIPI_ITEST_HELPER" httpd "$HTTP_PORT" "$WORK/httproot" >"$WORK/httpd.log" 2>&1 &
HTTPD=$!
trap 'kill $HTTPD 2>/dev/null; rm -rf "$WORK"' EXIT
i=0
while ! grep -q "Serving HTTP" "$WORK/httpd.log" 2>/dev/null; do
    i=$((i + 1))
    [ "$i" -lt 200 ] || fail "adts: http server on $HTTP_PORT never became ready"
    sleep 0.05
done

tsp -I ip $MCAST:$PORT --local-address 127.0.0.1 --receive-timeout 60000 \
    -O file "$cap" >"$WORK/tsp.log" 2>&1 &
TSPID=$!
wait_until 30 udp_port_busy $PORT || fail "tsp capture never bound $PORT"

timeout 10 "$BIN" -O lo -m $MCAST:$PORT -i "http://127.0.0.1:$HTTP_PORT/stream.aac" -s "ADTS Station" \
    >"$WORK/dipiradiohead.log" 2>&1 || true

sleep 0.5
kill $TSPID 2>/dev/null
wait $TSPID 2>/dev/null || true
kill $HTTPD 2>/dev/null || true

[ -s "$cap" ] || fail "adts: no packets captured (see $WORK/dipiradiohead.log)"

tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze.log" || fail "adts: tsanalyze failed, see $WORK/tsanalyze.log"

cc_errors=$(jq '[.pids[]? | .cc_errors // 0] | add // 0' "$report")
[ "${cc_errors:-0}" = "0" ] || fail "adts: $cc_errors continuity-counter errors in capture"

service_name=$(jq -r '.services[0].name // empty' "$report")
[ "$service_name" = "ADTS Station" ] || fail "adts: expected service name 'ADTS Station', got '$service_name'"

audio_desc=$(jq -r '.pids[] | select(.id==257 and .audio==true) | .description // empty' "$report")
case "$audio_desc" in
    *AAC*) ;;
    *) fail "adts: expected an AAC audio component on pid 0x0101, got '$audio_desc'" ;;
esac

audio_pes=$(jq -r '.pids[] | select(.id==257) | .pes // 0' "$report")
[ "${audio_pes:-0}" -ge 1 ] || fail "adts: no PES packets carried on the audio pid"

echo "OK"

#EOF
