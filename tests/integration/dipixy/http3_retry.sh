#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in curl openssl; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done
require_itest_helper

curl -V | grep -q "HTTP3" || skip "curl was not built with HTTP/3 support"

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

FPB=$(free_port_block 2)
TLSPORT=$((FPB + 0))
HTTPPORT=$((FPB + 1))

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

DPID=""
start() {
    mode=$1
    timeout 30 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" --h3-retry "$mode" \
        --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/dipixy_$mode.log" 2>&1 &
    DPID=$!
    sleep 0.7
    grep -q "http3: quic context ready" "$WORK/dipixy_$mode.log" || {
        stop
        skip "dipixy was built without HTTP/3"
    }
    return 0
}

stop() {
    [ -n "$DPID" ] || return
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    DPID=""
}

probe() {
    what=$1
    "$DVBIPI_ITEST_HELPER" quic-probe "$TLSPORT" "$what" 2>"$WORK/probe.err" || fail "probe $what failed: $(cat "$WORK/probe.err")"
    return 0
}

expect() {
    mode=$1
    what=$2
    want=$3
    got=$(probe "$what")
    [ "$got" = "$want" ] || fail "retry=$mode probe '$what': expected '$want', got '$got'"
    return 0
}

fetch() {
    mode=$1
    curl -sk --http3-only --max-time 10 -o /dev/null -w "%{http_version} %{http_code}\n" \
        "https://127.0.0.1:$TLSPORT/nonexistent" >"$WORK/curl.out" 2>"$WORK/curl.err" \
        || fail "retry=$mode: curl failed, see $WORK/curl.err and $WORK/dipixy_$mode.log"
    [ "$(cat "$WORK/curl.out")" = "3 404" ] || fail "retry=$mode: unexpected curl result: $(cat "$WORK/curl.out")"
    return 0
}

start always
expect always initial retry
expect always badtoken initial-close
expect always version version-negotiation
expect always short reset
fetch always
stop

start off
expect off initial none
expect off version version-negotiation
expect off short reset
fetch off
stop

start auto
expect auto initial none
fetch auto
stop

echo "OK"
