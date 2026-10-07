#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in curl openssl nc; do
    command -v "$t" >/dev/null 2>&1 || skip "'$t' not found on PATH"
done

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

PORT=$(free_tcp_port)

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 -subj "/CN=host-a" >"$WORK/openssl_a.log" 2>&1 || fail "cert a generation failed, see $WORK/openssl_a.log"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key_b.pem" -out "$WORK/cert_b.pem" -days 1 -subj "/CN=host-b" >"$WORK/openssl_b.log" 2>&1 || fail "cert b generation failed, see $WORK/openssl_b.log"

timeout 5 "$BIN" -S "$WORK/missing.sock" -l "127.0.0.1:$PORT" --tls-cert "$WORK/nope.pem" --tls-key "$WORK/nope_key.pem" >"$WORK/missing.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "missing cert files: expected exit 1, got $rc (see $WORK/missing.log)"
[ ! -e "$WORK/missing.sock" ] || fail "socket created although the TLS setup failed"

timeout 5 "$BIN" -S "$WORK/mismatch.sock" -l "127.0.0.1:$PORT" --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key_b.pem" >"$WORK/mismatch.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "mismatched cert and key: expected exit 1, got $rc (see $WORK/mismatch.log)"

"$BIN" -S "$WORK/plain.sock" -l "127.0.0.1:$PORT" >"$WORK/plain.log" 2>&1 &
PLAIN=$!
trap 'kill $PLAIN $TLS 2>/dev/null; rm -rf "$WORK"' EXIT
wait_until 5 port_open "$PORT" || fail "plain instance never listened on $PORT (see $WORK/plain.log)"
kill -USR1 "$PLAIN"
wait_until 5 log_has "$WORK/plain.log" "no TLS configured, ignoring" || fail "no ignore message after SIGUSR1 without TLS (see $WORK/plain.log)"
code=$(curl -s -o /dev/null -w "%{http_code}" "http://127.0.0.1:$PORT/metrics")
[ "$code" = "200" ] || fail "plain instance stopped serving after SIGUSR1 (HTTP $code)"
kill -TERM "$PLAIN"
wait "$PLAIN"
rc=$?
[ "$rc" = "0" ] || fail "plain instance: expected exit 0 on SIGTERM, got $rc"

"$BIN" -S "$WORK/tls.sock" -l "127.0.0.1:$PORT" --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/tls.log" 2>&1 &
TLS=$!
wait_until 5 port_open "$PORT" || fail "tls instance never listened on $PORT (see $WORK/tls.log)"
cp "$WORK/cert_b.pem" "$WORK/cert.pem"
kill -USR1 "$TLS"
wait_until 5 log_has "$WORK/tls.log" "TLS cert reload failed" || fail "no reload failure message for a mismatched cert and key (see $WORK/tls.log)"
code=$(curl -sk -o /dev/null -w "%{http_code}" "https://127.0.0.1:$PORT/metrics")
[ "$code" = "200" ] || fail "tls instance stopped serving after a failed reload (HTTP $code)"
cn=$(echo | openssl s_client -connect "127.0.0.1:$PORT" 2>/dev/null | openssl x509 -noout -subject)
echo "$cn" | grep -q "CN *= *host-a" || fail "served cert changed after a failed reload: '$cn'"

echo "OK"

#EOF
