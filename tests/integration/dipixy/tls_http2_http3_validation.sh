#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in ffmpeg curl openssl tsanalyze jq; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP2" || fail "curl was not built with HTTP/2 support"
curl_has_http3=0
curl -V | grep -q "HTTP3" && curl_has_http3=1

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

MCAST=239.255.9.25
MPORT=18105
TLSPORT=19206
HTTPPORT=19207

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

ffmpeg -hide_banner -loglevel error -re -stream_loop -1 -f lavfi -i "testsrc=size=320x240:rate=25" \
    -f lavfi -i "sine=frequency=1000" -t 20 \
    -c:v libx264 -preset ultrafast -c:a aac -f mpegts \
    "udp://$MCAST:$MPORT?pkt_size=1316" >"$WORK/ffmpeg.log" 2>&1 &
FFPID=$!
sleep 0.5

timeout 15 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" \
    --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.7

url="https://127.0.0.1:$TLSPORT/udp/$MCAST:$MPORT/ts"

stop_bg() {
    kill $FFPID 2>/dev/null
    wait $FFPID 2>/dev/null
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
}

check_cap() {
    cap=$1
    label=$2
    report="$cap.json"
    tsanalyze --json "$cap" >"$report" 2>"$WORK/tsanalyze_$(basename "$cap").log" \
        || fail "tsanalyze failed on $cap"
    services=$(jq '.services | length' "$report")
    [ "${services:-0}" -ge 1 ] || fail "$label: no service found"
}

h2cap="$WORK/h2.ts"
timeout 5 curl -skv --http2 -o "$h2cap" "$url" 2>"$WORK/curl_h2.log"
grep -q "using HTTP/2" "$WORK/curl_h2.log" || fail "curl did not negotiate HTTP/2, see $WORK/curl_h2.log and $WORK/dipixy.log"
[ -s "$h2cap" ] || fail "no packets captured over HTTP/2"
check_cap "$h2cap" "http2"

if [ "$curl_has_http3" -eq 0 ]; then
    stop_bg
    skip "curl was not built with HTTP/3 support"
fi

h3cap="$WORK/h3.ts"
timeout 5 curl -skv --http3 -o "$h3cap" "$url" 2>"$WORK/curl_h3.log"
grep -q "using HTTP/3" "$WORK/curl_h3.log" || fail "curl did not negotiate HTTP/3, see $WORK/curl_h3.log and $WORK/dipixy.log"
[ -s "$h3cap" ] || fail "no packets captured over HTTP/3"

stop_bg

check_cap "$h3cap" "http3"

echo "OK"

#EOF
