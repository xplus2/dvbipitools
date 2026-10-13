#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl ffprobe jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 2)
MPORT=$((FPB + 0))
HTTPPORT=$((FPB + 1))
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
assert_contains "$mpd" 'contentType="video"' "manifest missing video AdaptationSet"
assert_contains "$mpd" 'contentType="audio"' "manifest missing audio AdaptationSet"
assert_contains "$mpd" 'media="dsegv\$Time\$.m4s"' "manifest missing video SegmentTemplate media pattern"
assert_contains "$mpd" 'media="dsega\$Time\$.m4s"' "manifest missing audio SegmentTemplate media pattern"
assert_contains "$mpd" 'initialization="dinitv.mp4"' "manifest missing video init"
assert_contains "$mpd" 'initialization="dinita.mp4"' "manifest missing audio init"
assert_contains "$mpd" 'audioSamplingRate="' "manifest missing audioSamplingRate"
assert_contains "$mpd" "<AudioChannelConfiguration" "manifest missing AudioChannelConfiguration"
if grep -q 'codecs="[^"]*,' "$mpd"; then
    fail "manifest still lists muxed codecs in one Representation"
fi

first_t=$(grep -oE '<S t="[0-9]+"' "$mpd" | head -1 | grep -oE '[0-9]+')
[ -n "$first_t" ] || fail "could not derive a segment time from SegmentTimeline"

for track in v a; do
    case $track in v) kind=video; other=audio ;; a) kind=audio; other=video ;; esac
    timeout 10 curl -s -o "$WORK/init-$track.mp4" "$BASE/dinit$track.mp4"
    timeout 10 curl -s -o "$WORK/seg-$track.m4s" "$BASE/dseg$track$first_t.m4s"
    [ -s "$WORK/init-$track.mp4" ] || fail "empty dinit$track.mp4"
    [ -s "$WORK/seg-$track.m4s" ] || fail "empty dseg$track$first_t.m4s"
    cat "$WORK/init-$track.mp4" "$WORK/seg-$track.m4s" >"$WORK/combined-$track.mp4"
    probe="$WORK/probe-$track.json"
    ffprobe -v error -print_format json -show_streams "$WORK/combined-$track.mp4" >"$probe" 2>"$WORK/ffprobe-$track.log" \
        || fail "ffprobe failed on dinit$track.mp4+dseg$track$first_t.m4s, see $WORK/ffprobe-$track.log"
    got=$(jq -r "[.streams[] | select(.codec_type == \"$kind\")] | length" "$probe")
    [ "$got" = "1" ] || fail "expected one $kind stream in $track track, got '$got'"
    got=$(jq -r "[.streams[] | select(.codec_type == \"$other\")] | length" "$probe")
    [ "$got" = "0" ] || fail "$track track carries $got $other stream(s), expected none"
done

video_codec=$(jq -r '[.streams[] | select(.codec_type == "video")][0].codec_name // empty' "$WORK/probe-v.json")
[ "$video_codec" = "h264" ] || fail "expected h264 video in DASH segment, got '$video_codec'"

audio_codec=$(jq -r '[.streams[] | select(.codec_type == "audio")][0].codec_name // empty' "$WORK/probe-a.json")
[ "$audio_codec" = "aac" ] || fail "expected aac audio in DASH segment, got '$audio_codec'"

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

echo "OK"

#EOF
