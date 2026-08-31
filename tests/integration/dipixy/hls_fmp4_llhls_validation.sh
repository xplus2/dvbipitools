#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl ffprobe tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

MCAST=239.255.9.23
MPORT=18103
HTTPPORT=19204
BASE="http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 30 \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 25 "$BIN" -l "127.0.0.1:$HTTPPORT" --segment-size 2 --segment-count 3 --hls-part-size 0.5 \
    >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.5

fmp4_playlist="$WORK/index_fmp4.m3u8"
timeout 15 curl -s -o "$fmp4_playlist" "$BASE/hls-fmp4"
sleep 4
timeout 15 curl -s -o "$fmp4_playlist" "$BASE/hls-fmp4"
assert_contains "$fmp4_playlist" "#EXT-X-MAP:URI=\"init.mp4\"" "hls-fmp4 playlist missing init.mp4 map"

fmp4_seg=$(grep -oE 'seg[0-9]+\.m4s' "$fmp4_playlist" | head -1)
[ -n "$fmp4_seg" ] || fail "no fmp4 segment reference in playlist"

timeout 10 curl -s -o "$WORK/init.mp4" "$BASE/init.mp4"
timeout 10 curl -s -o "$WORK/$fmp4_seg" "$BASE/$fmp4_seg"
[ -s "$WORK/init.mp4" ] || fail "empty init.mp4"
[ -s "$WORK/$fmp4_seg" ] || fail "empty $fmp4_seg"

cat "$WORK/init.mp4" "$WORK/$fmp4_seg" >"$WORK/combined.mp4"
probe="$WORK/probe.json"
ffprobe -v error -print_format json -show_streams "$WORK/combined.mp4" >"$probe" 2>"$WORK/ffprobe.log" \
    || fail "ffprobe failed on init.mp4+$fmp4_seg, see $WORK/ffprobe.log"

video_codec=$(jq -r '[.streams[] | select(.codec_type == "video")][0].codec_name // empty' "$probe")
[ "$video_codec" = "h264" ] || fail "expected h264 video in fmp4 segment, got '$video_codec'"

audio_codec=$(jq -r '[.streams[] | select(.codec_type == "audio")][0].codec_name // empty' "$probe")
[ "$audio_codec" = "aac" ] || fail "expected aac audio in fmp4 segment, got '$audio_codec'"

ll_playlist="$WORK/index_ll.m3u8"
timeout 15 curl -s -o "$ll_playlist" "$BASE/llhls"
sleep 4
timeout 15 curl -s -o "$ll_playlist" "$BASE/llhls"
assert_contains "$ll_playlist" "#EXT-X-PART-INF" "llhls playlist missing PART-INF"

part=$(grep -oE 'seg[0-9]+\.[0-9]+\.ts' "$ll_playlist" | head -1)
[ -n "$part" ] || fail "no LL-HLS part reference in playlist"

timeout 10 curl -s -o "$WORK/$part" "$BASE/$part"
[ -s "$WORK/$part" ] || fail "empty LL-HLS part $part"

part_report="$WORK/part.json"
tsanalyze --json "$WORK/$part" >"$part_report" 2>"$WORK/tsanalyze_part.log" \
    || fail "tsanalyze failed on LL-HLS part $part, see $WORK/tsanalyze_part.log"
part_services=$(jq '.services | length' "$part_report")
[ "${part_services:-0}" -ge 1 ] || fail "LL-HLS part $part: no service found"

kill $FFPID 2>/dev/null
wait $FFPID 2>/dev/null
kill $DPID 2>/dev/null
wait $DPID 2>/dev/null

echo "OK"

#EOF
