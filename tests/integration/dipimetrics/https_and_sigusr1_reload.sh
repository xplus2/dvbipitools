#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in curl openssl; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

HTTPPORT=19309
SOCK="$WORK/metrics.sock"

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 -subj "/CN=host-a" >"$WORK/openssl_a.log" 2>&1 || fail "cert a generation failed, see $WORK/openssl_a.log"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key_b.pem" -out "$WORK/cert_b.pem" -days 1 -subj "/CN=host-b" >"$WORK/openssl_b.log" 2>&1 || fail "cert b generation failed, see $WORK/openssl_b.log"
timeout 12 "$BIN" -S "$SOCK" -l "127.0.0.1:$HTTPPORT" --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" -v >"$WORK/dipimetrics.log" 2>&1 &
MPID=$!
sleep 0.5

body="$WORK/metrics.txt"
code=$(curl -sk -o "$body" -w "%{http_code}" "https://127.0.0.1:$HTTPPORT/metrics")
[ "$code" = "200" ] || fail "GET https:.../metrics: expected HTTP 200, got $code (see $WORK/dipimetrics.log)"
assert_contains "$body" "dvbipi_metrics_instances" "openmetrics body over https"

code=$(curl -sk -o /dev/null -w "%{http_code}" "https://127.0.0.1:$HTTPPORT/nope")
[ "$code" = "404" ] || fail "GET https:.../nope: expected HTTP 404, got $code"

curl -s -m 2 "http://127.0.0.1:$HTTPPORT/metrics" >/dev/null 2>&1 && fail "plain HTTP request against a TLS-only listener unexpectedly succeeded"

cn=$(echo | openssl s_client -connect "127.0.0.1:$HTTPPORT" 2>/dev/null | openssl x509 -noout -subject)
echo "$cn" | grep -q "CN *= *host-a" || fail "served cert before reload: expected CN=host-a, got '$cn'"

cp "$WORK/cert_b.pem" "$WORK/cert.pem"
cp "$WORK/key_b.pem" "$WORK/key.pem"
kill -USR1 "$MPID"

i=0
while [ $i -lt 30 ]; do
    cn=$(echo | openssl s_client -connect "127.0.0.1:$HTTPPORT" 2>/dev/null | openssl x509 -noout -subject)
    echo "$cn" | grep -q "CN *= *host-b" && break
    i=$((i + 1))
    sleep 0.1
done
echo "$cn" | grep -q "CN *= *host-b" || fail "served cert after reload: expected CN=host-b, got '$cn' (see $WORK/dipimetrics.log)"

kill $MPID 2>/dev/null
wait $MPID 2>/dev/null

echo "OK"

#EOF
