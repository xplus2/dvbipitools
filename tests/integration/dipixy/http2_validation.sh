#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl openssl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP2" || fail "curl was not built with HTTP/2 support"

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

MCAST=$(unique_mcast 61)
FPB=$(free_port_block 3)
MPORT=$((FPB + 0))
TLSPORT=$((FPB + 1))
HTTPPORT=$((FPB + 2))
CORS_ORIGIN=https://player.example.test

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 20 \
    -g 50 -sc_threshold 0 -force_key_frames "expr:gte(t,n_forced*2)" \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 18 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" --no-http3 \
    --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" \
    --segment-size 2 --segment-count 3 --cors-origin "$CORS_ORIGIN" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.7

BASE="https://127.0.0.1:$TLSPORT/udp/$MCAST:$MPORT"

stop_bg() {
    kill $FFPID 2>/dev/null
    wait $FFPID 2>/dev/null
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    return $?
}

h2curl() {
    timeout 10 curl -sk --http2 "$@"
    return $?
}

status_line=$(h2curl -o /dev/null -w '%{http_code} %{http_version}' "https://127.0.0.1:$TLSPORT/nonexistent")
[ "$status_line" = "404 2" ] || fail "unknown path: expected '404 2', got '$status_line'"

status_line=$(h2curl -X POST -o /dev/null -w '%{http_code} %{http_version}' "$BASE/hls")
[ "$status_line" = "405 2" ] || fail "POST: expected '405 2', got '$status_line'"

h2cap="$WORK/h2.ts"
h2curl --max-time 4 -o "$h2cap" "$BASE/ts"
[ -s "$h2cap" ] || fail "no packets captured over HTTP/2 ts push"
tsanalyze --json "$h2cap" >"$WORK/h2.json" 2>"$WORK/tsanalyze.log" || fail "tsanalyze failed, see $WORK/tsanalyze.log"
services=$(jq '.services | length' "$WORK/h2.json")
[ "${services:-0}" -ge 1 ] || fail "ts push over HTTP/2: no service found"

h2curl -o /dev/null "$BASE/hls"
sleep 5

playlist="$WORK/index.m3u8"
h2curl -D "$WORK/playlist.hdr" -o "$playlist" "$BASE/hls"
assert_contains "$WORK/playlist.hdr" "^HTTP/2 200" "hls playlist over HTTP/2"
assert_contains "$playlist" "#EXTM3U" "hls playlist"
seg=$(grep -oE 'seg[0-9]+\.ts' "$playlist" | head -1)
[ -n "$seg" ] || fail "no segment reference in playlist"

grep -qi "^access-control-allow-origin: $CORS_ORIGIN" "$WORK/playlist.hdr" && fail "cors: allow-origin sent without an Origin header"
h2curl -D "$WORK/cors.hdr" -o /dev/null -H "Origin: $CORS_ORIGIN" "$BASE/hls"
grep -qi "^access-control-allow-origin: $CORS_ORIGIN" "$WORK/cors.hdr" || fail "cors: missing allow-origin for listed origin, see $WORK/cors.hdr"
grep -qi "^vary: Origin" "$WORK/cors.hdr" || fail "cors: missing Vary: Origin"
h2curl -D "$WORK/cors_other.hdr" -o /dev/null -H "Origin: https://other.example.test" "$BASE/hls"
! grep -qi "^access-control-allow-origin:" "$WORK/cors_other.hdr" || fail "cors: allow-origin sent for unlisted origin"

h2curl -D "$WORK/seg.hdr" -o "$WORK/$seg" "$BASE/$seg"
assert_contains "$WORK/seg.hdr" "^HTTP/2 200" "segment over HTTP/2"
[ -s "$WORK/$seg" ] || fail "empty segment $seg"
first_byte=$(head -c 1 "$WORK/$seg" | od -An -tx1 | tr -d ' ')
[ "$first_byte" = "47" ] || fail "segment $seg does not start with a TS sync byte"
seg_len=$(grep -i "^content-length:" "$WORK/seg.hdr" | tr -d '\r' | awk '{print $2}')
[ "$seg_len" = "$(wc -c <"$WORK/$seg" | tr -d ' ')" ] || fail "segment content-length $seg_len does not match body"

etag=$(grep -i "^etag:" "$WORK/seg.hdr" | tr -d '\r' | cut -d' ' -f2-)
if [ -n "$etag" ]; then
    status_line=$(h2curl -o /dev/null -w '%{http_code}' -H "If-None-Match: $etag" "$BASE/$seg")
    [ "$status_line" = "304" ] || fail "conditional GET with matching etag: expected 304, got $status_line"
fi

head_size=$(h2curl -I -D "$WORK/head.hdr" -o /dev/null -w '%{size_download}' "$BASE/$seg")
assert_contains "$WORK/head.hdr" "^HTTP/2 200" "HEAD over HTTP/2"
assert_contains "$WORK/head.hdr" "^content-length: $seg_len" "HEAD content-length"
[ "$head_size" = "0" ] || fail "HEAD response carried $head_size body bytes"

timeout 15 curl -sk --http2 --parallel --parallel-immediate \
    -o "$WORK/par1.ts" "$BASE/$seg" \
    -o "$WORK/par2.m3u8" "$BASE/hls" \
    -o "$WORK/par3.ts" "$BASE/$seg" \
    -o "$WORK/par4.bin" "https://127.0.0.1:$TLSPORT/nonexistent" \
    -w '%{http_code} %{http_version}\n' >"$WORK/parallel.txt" 2>"$WORK/parallel.log" \
    || fail "parallel requests failed, see $WORK/parallel.log"
[ "$(grep -c '^200 2$' "$WORK/parallel.txt")" = "3" ] || fail "parallel: expected three '200 2' results, see $WORK/parallel.txt"
[ "$(grep -c '^404 2$' "$WORK/parallel.txt")" = "1" ] || fail "parallel: expected one '404 2' result, see $WORK/parallel.txt"
cmp -s "$WORK/par1.ts" "$WORK/par3.ts" || fail "parallel: the same segment differs between streams"

stop_bg

echo "OK"

#EOF
