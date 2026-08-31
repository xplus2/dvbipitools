#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.9.21
MPORT=18101
HTTPPORT=19201

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 20 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 15 "$BIN" -l "127.0.0.1:$HTTPPORT" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

plain="$WORK/plain.ts"
plain_report="$WORK/plain.json"
timeout 6 curl -s -o "$plain" "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/ts"
[ -s "$plain" ] || fail "no packets in unfiltered capture, see $WORK/dipixy.log and $WORK/ffmpeg.log"
tsanalyze --json "$plain" >"$plain_report" 2>"$WORK/tsanalyze_plain.log" \
    || fail "tsanalyze failed on unfiltered capture, see $WORK/tsanalyze_plain.log"

audio_pid=$(jq -r '[.pids[] | select(.audio == true)][0].id // empty' "$plain_report")
[ -n "$audio_pid" ] || fail "could not find an audio PID in unfiltered capture"

audio_packets=$(jq --argjson pid "$audio_pid" '[.pids[] | select(.id == $pid) | .packets.total][0] // 0' "$plain_report")
[ "${audio_packets:-0}" -gt 0 ] || fail "audio PID $audio_pid has no packets in unfiltered capture"

filtered="$WORK/filtered.ts"
filtered_report="$WORK/filtered.json"
timeout 6 curl -s -o "$filtered" "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/ts?filter=$audio_pid"

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

[ -s "$filtered" ] || fail "no packets in filtered capture"
tsanalyze --json "$filtered" >"$filtered_report" 2>"$WORK/tsanalyze_filtered.log" \
    || fail "tsanalyze failed on filtered capture, see $WORK/tsanalyze_filtered.log"

filtered_audio_packets=$(jq --argjson pid "$audio_pid" '[.pids[] | select(.id == $pid) | .packets.total][0] // 0' "$filtered_report")
[ "${filtered_audio_packets:-1}" = "0" ] || fail "audio PID $audio_pid still carried $filtered_audio_packets packets after ?filter=$audio_pid"

video_present=$(jq '[.pids[] | select(.video == true)] | length' "$filtered_report")
[ "${video_present:-0}" -ge 1 ] || fail "video PID unexpectedly dropped by ?filter=$audio_pid"

pmt_still_lists_audio=$(jq --argjson pid "$audio_pid" '(.services[0].pids // []) | index($pid) != null' "$filtered_report")
[ "$pmt_still_lists_audio" = "false" ] || fail "PMT still lists audio PID $audio_pid as a service component after ?filter=$audio_pid"

echo "OK"

#EOF
