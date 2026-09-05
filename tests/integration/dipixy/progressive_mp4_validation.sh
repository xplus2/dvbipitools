#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl ffprobe openssl jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP2" || fail "curl was not built with HTTP/2 support"
curl_has_http3=0
curl -V | grep -q "HTTP3" && curl_has_http3=1

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

MCAST=239.255.9.31
MPORT=18109
HTTPPORT=19216
TLSPORT=19217

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 40 \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 30 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" \
    --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" \
    --segment-size 2 --segment-count 3 >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 1

stop_bg() {
    kill $FFPID 2>/dev/null
    wait $FFPID 2>/dev/null
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
}

check_progressive_mp4() {
    cap=$1
    label=$2
    [ -s "$cap" ] || fail "$label: empty capture"
    head -c 12 "$cap" | grep -aq "ftyp" || fail "$label: capture does not start with an ftyp box"
    grep -aq "moov" "$cap" || fail "$label: no moov box (init segment) found"
    grep -aq "moof" "$cap" || fail "$label: no moof box (live fragment) found"
    probe="$cap.json"
    ffprobe -v error -print_format json -show_streams "$cap" >"$probe" 2>"$WORK/ffprobe_$(basename "$cap").log" \
        || fail "$label: ffprobe failed, see $WORK/ffprobe_$(basename "$cap").log"
    video_codec=$(jq -r '[.streams[] | select(.codec_type == "video")][0].codec_name // empty' "$probe")
    [ "$video_codec" = "h264" ] || fail "$label: expected h264 video, got '$video_codec'"
    audio_codec=$(jq -r '[.streams[] | select(.codec_type == "audio")][0].codec_name // empty' "$probe")
    [ "$audio_codec" = "aac" ] || fail "$label: expected aac audio, got '$audio_codec'"
}

h1cap="$WORK/h1.mp4"
timeout 8 curl -s -o "$h1cap" "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/mp4"
check_progressive_mp4 "$h1cap" "H1"
head_hdrs="$WORK/head.txt"
timeout 5 curl -sI "http://127.0.0.1:$HTTPPORT/udp/$MCAST:$MPORT/mp4" >"$head_hdrs"
assert_contains "$head_hdrs" "Content-Type: video/mp4" "HEAD response missing Content-Type"
h2cap="$WORK/h2.mp4"
timeout 8 curl -s -k -N --http2 -o "$h2cap" "https://127.0.0.1:$TLSPORT/udp/$MCAST:$MPORT/mp4"
check_progressive_mp4 "$h2cap" "H2"

if [ "$curl_has_http3" -eq 1 ]; then
    h3cap="$WORK/h3.mp4"
    timeout 8 curl -s -k -N --http3-only -o "$h3cap" "https://127.0.0.1:$TLSPORT/udp/$MCAST:$MPORT/mp4"
    check_progressive_mp4 "$h3cap" "H3"
fi

kill -0 $DPID 2>/dev/null || fail "dipixy exited/crashed after progressive MP4 clients disconnected"

stop_bg

echo "OK"

#EOF
