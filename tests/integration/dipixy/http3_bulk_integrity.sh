#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl openssl cmp; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP3" || skip "curl was not built with HTTP/3 support"

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

MCAST=239.255.9.26
MPORT=18106
TLSPORT=19246
HTTPPORT=19247

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

# CBR so segments are several MB, many GSO batches each
ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc2=size=640x360:rate=25" \
    -c:v libx264 -preset ultrafast -x264-params "nal-hrd=cbr:force-cvbr=1" -b:v 30M -minrate 30M -maxrate 30M -bufsize 15M \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" -t 40 -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

DPID=""
stop_srv() {
    [ -n "$DPID" ] || return
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    DPID=""
}

cleanup_all() {
    stop_srv
    kill $FFPID 2>/dev/null
    wait $FFPID 2>/dev/null
}

run_case() {
    name=$1
    shift
    timeout 30 "$BIN" -j 4 -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" \
        --segment-size 2 --segment-count 3 "$@" >"$WORK/dipixy_$name.log" 2>&1 &
    DPID=$!
    sleep 0.7
    grep -q "http3: quic context ready" "$WORK/dipixy_$name.log" || {
        cleanup_all
        skip "dipixy was built without HTTP/3"
    }
    base="https://127.0.0.1:$TLSPORT/udp/$MCAST:$MPORT"
    curl -sk -o "$WORK/pl.m3u8" "$base/hls"
    sleep 6
    curl -sk -o "$WORK/pl.m3u8" "$base/hls"
    segs=$(grep -E '^seg[0-9]+\.ts$' "$WORK/pl.m3u8")
    [ -n "$segs" ] || fail "$name: no segments in playlist"
    n=0
    for seg in $segs; do
        curl -sk --http1.1 -o "$WORK/h1_$seg" "$base/$seg" || fail "$name: http1 fetch of $seg failed"
        curl -sk --http3-only --max-time 20 -o "$WORK/h3_$seg" "$base/$seg" || fail "$name: http3 fetch of $seg failed"
        [ "$(wc -c <"$WORK/h1_$seg")" -gt 1000000 ] || fail "$name: segment $seg smaller than 1 MB"
        cmp -s "$WORK/h1_$seg" "$WORK/h3_$seg" || fail "$name: $seg differs between http1 and http3"
        n=$((n + 1))
    done
    [ $n -ge 1 ] || fail "$name: nothing compared"
    stop_srv
}

run_case default
run_case tuned --h3-cc bbr --h3-window 64 --h3-max-udp-payload 1300

cleanup_all
echo "OK"
