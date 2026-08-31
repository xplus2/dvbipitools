#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.9.22
MPORT=18102
HTTPPORT=19203
BASE="http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 30 \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 25 "$BIN" -l "127.0.0.1:$HTTPPORT" --segment-size 2 --segment-count 3 >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

playlist="$WORK/index.m3u8"
timeout 15 curl -s -o "$playlist" "$BASE/hls"
[ -s "$playlist" ] || fail "empty/missing HLS playlist, see $WORK/dipixy.log"
assert_contains "$playlist" "#EXTM3U" "not a valid m3u8 playlist"

sleep 4

timeout 15 curl -s -o "$playlist" "$BASE/hls"
segs=$(grep -E '^seg[0-9]+\.ts$' "$playlist")
[ -n "$segs" ] || fail "no segment references found in playlist after warm-up"

n=0
for seg in $segs; do
    segfile="$WORK/$seg"
    timeout 10 curl -s -o "$segfile" "$BASE/$seg"
    [ -s "$segfile" ] || fail "segment $seg fetched empty"
    report="$WORK/$seg.json"
    tsanalyze --json "$segfile" >"$report" 2>"$WORK/tsanalyze_$seg.log" \
        || fail "tsanalyze failed on $seg, see $WORK/tsanalyze_$seg.log"
    services=$(jq '.services | length' "$report")
    [ "${services:-0}" -ge 1 ] || fail "segment $seg: no service found"
    n=$((n + 1))
done

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

[ "$n" -ge 1 ] || fail "validated 0 segments"

echo "OK"

#EOF
