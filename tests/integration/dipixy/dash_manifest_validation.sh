#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl ffprobe jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.9.24
MPORT=18104
HTTPPORT=19205
BASE="http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 30 \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 25 "$BIN" -l "127.0.0.1:$HTTPPORT" --segment-size 2 --segment-count 3 \
    >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

mpd="$WORK/manifest.mpd"
timeout 15 curl -s -o "$mpd" "$BASE/dash"
sleep 4
timeout 15 curl -s -o "$mpd" "$BASE/dash"

[ -s "$mpd" ] || fail "empty/missing DASH manifest, see $WORK/dipixy.log"
assert_contains "$mpd" "<MPD" "not a valid DASH manifest"
assert_contains "$mpd" 'media="dseg\$Time\$.m4s"' "manifest missing SegmentTemplate media pattern"

first_seg=$(grep -oE '<S[^/]*/>' "$mpd" | while IFS= read -r line; do
    t=$(echo "$line" | grep -oE 't="[0-9]+"' | grep -oE '[0-9]+')
    d=$(echo "$line" | grep -oE 'd="[0-9]+"' | grep -oE '[0-9]+')
    [ -n "$t" ] && time=$t
    echo "dseg${time}.m4s"
    time=$((time + d))
done | head -1)

[ -n "$first_seg" ] || fail "could not derive a segment name from SegmentTimeline"

timeout 10 curl -s -o "$WORK/init.mp4" "$BASE/init.mp4"
timeout 10 curl -s -o "$WORK/$first_seg" "$BASE/$first_seg"
[ -s "$WORK/init.mp4" ] || fail "empty init.mp4"
[ -s "$WORK/$first_seg" ] || fail "empty $first_seg"

cat "$WORK/init.mp4" "$WORK/$first_seg" >"$WORK/combined.mp4"
probe="$WORK/probe.json"
ffprobe -v error -print_format json -show_streams "$WORK/combined.mp4" >"$probe" 2>"$WORK/ffprobe.log" \
    || fail "ffprobe failed on init.mp4+$first_seg, see $WORK/ffprobe.log"

video_codec=$(jq -r '[.streams[] | select(.codec_type == "video")][0].codec_name // empty' "$probe")
[ "$video_codec" = "h264" ] || fail "expected h264 video in DASH segment, got '$video_codec'"

audio_codec=$(jq -r '[.streams[] | select(.codec_type == "audio")][0].codec_name // empty' "$probe")
[ "$audio_codec" = "aac" ] || fail "expected aac audio in DASH segment, got '$audio_codec'"

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

echo "OK"

#EOF
