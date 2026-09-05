#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v ffmpeg >/dev/null 2>&1 || fail "required tool 'ffmpeg' not found on PATH"
command -v ffprobe >/dev/null 2>&1 || fail "required tool 'ffprobe' not found on PATH"
command -v openssl >/dev/null 2>&1 || fail "required tool 'openssl' not found on PATH"

DIPITVHEAD=$(echo "$BIN" | sed 's#/dipidescramble\([^/]*\)$#/../dipitvhead/dipitvhead\1#')
[ -x "$DIPITVHEAD" ] || DIPITVHEAD="./dipitvhead"
[ -x "$DIPITVHEAD" ] || fail "cannot locate dipitvhead binary (tried $DIPITVHEAD)"

MCAST=239.255.7.55
PORT=17755

RECEIVERS="$WORK/receivers"
mkdir -p "$RECEIVERS"
PRIVKEY="$WORK/receiver1.key"
openssl genrsa -out "$PRIVKEY" 2048 >"$WORK/openssl-genrsa.log" 2>&1 || fail "openssl genrsa failed"
openssl rsa -in "$PRIVKEY" -pubout -out "$RECEIVERS/receiver1.pem" >"$WORK/openssl-pubout.log" 2>&1 || fail "openssl rsa -pubout failed"

out="$WORK/descrambled.ts"

"$BIN" -i "udp://@$MCAST:$PORT" -I lo --biss2-ca-key "$PRIVKEY" \
    -o "$out" -f ts >"$WORK/dipidescramble.log" 2>&1 &
DESCPID=$!
sleep 0.3

ffmpeg -hide_banner -loglevel error -re -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 6 \
    -c:v libx264 -preset ultrafast -g 25 -x264-params repeat-headers=1 -c:a aac -f mpegts - 2>"$WORK/ffmpeg.log" | \
timeout 8 "$DIPITVHEAD" -O lo -u -m $MCAST:$PORT -i - -s "BISS-CA Test" \
    --biss2-ca-receivers "$RECEIVERS" --cas-cp-duration 1000 --cas-pids video,audio \
    >"$WORK/dipitvhead.log" 2>&1

sleep 1
kill $DESCPID 2>/dev/null
wait $DESCPID 2>/dev/null

assert_not_contains "$WORK/dipidescramble.log" "cannot load RSA private key" "dipidescramble --biss2-ca-key load"
assert_contains "$WORK/dipidescramble.log" "BISS Mode CA detected" "dipidescramble BISS-CA detection"
assert_contains "$WORK/dipidescramble.log" "CW updated (parity=even)" "dipidescramble resolved the even-parity SW from an ECM"
assert_contains "$WORK/dipidescramble.log" "CW updated (parity=odd)" "dipidescramble resolved the odd-parity SW from an ECM"

assert_not_contains "$WORK/dipitvhead.log" "biss-ca: loaded 0 entitled receiver" "dipitvhead receiver load"

[ -s "$out" ] || fail "dipidescramble: no output file produced"

# confirms actual decodability, not just non-empty output
ffprobe -v error -count_frames -show_entries stream=codec_type,nb_read_frames -of csv=p=0 "$out" >"$WORK/ffprobe.log" 2>"$WORK/ffprobe.err"
cat "$WORK/ffprobe.err" >&2
video_frames=$(grep '^video,' "$WORK/ffprobe.log" | head -1 | cut -d, -f2)
audio_frames=$(grep '^audio,' "$WORK/ffprobe.log" | head -1 | cut -d, -f2)
[ "${video_frames:-0}" -gt 0 ] 2>/dev/null || fail "descrambled output: video stream did not decode (0 frames)"
[ "${audio_frames:-0}" -gt 0 ] 2>/dev/null || fail "descrambled output: audio stream did not decode (0 frames)"

echo "OK"

#EOF
