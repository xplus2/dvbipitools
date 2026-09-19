#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in curl openssl; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

curl -V | grep -q "HTTP3" || skip "curl was not built with HTTP/3 support"

[ -f /etc/ssl/openssl.cnf ] && OPENSSL_CONF=/etc/ssl/openssl.cnf
export OPENSSL_CONF

TLSPORT=19216
HTTPPORT=19217
REQUESTS=40

openssl req -x509 -newkey rsa:2048 -nodes -keyout "$WORK/key.pem" -out "$WORK/cert.pem" -days 1 \
    -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
    >"$WORK/openssl.log" 2>&1 || fail "openssl cert generation failed, see $WORK/openssl.log"

# limit << REQUESTS: only stream-limit extension keeps 1 conn
timeout 40 "$BIN" -l "127.0.0.1:$HTTPPORT" -L "127.0.0.1:$TLSPORT" --h3-max-streams 8 \
    --tls-cert "$WORK/cert.pem" --tls-key "$WORK/key.pem" >"$WORK/dipixy.log" 2>&1 &
DPID=$!
sleep 0.7

stop_bg() {
    kill $DPID 2>/dev/null
    wait $DPID 2>/dev/null
    return 0
}

grep -q "http3: quic context ready" "$WORK/dipixy.log" || {
    stop_bg
    skip "dipixy was built without HTTP/3"
}

# one curl process, one QUIC conn
urls=""
i=0
while [ $i -lt $REQUESTS ]; do
    urls="$urls https://127.0.0.1:$TLSPORT/nonexistent$i"
    i=$((i + 1))
done

# shellcheck disable=SC2086
curl -sk --http3-only --max-time 30 -o /dev/null \
    -w "%{http_version} %{http_code} %{num_connects}\n" $urls >"$WORK/out.txt" 2>"$WORK/curl.err"
rc=$?
stop_bg

[ $rc -eq 0 ] || fail "curl rc=$rc after $(wc -l <"$WORK/out.txt") of $REQUESTS requests, see $WORK/curl.err and $WORK/dipixy.log"

done_n=$(wc -l <"$WORK/out.txt")
[ "$done_n" -eq $REQUESTS ] || fail "only $done_n of $REQUESTS requests completed"

bad=$(grep -vc "^3 404 " "$WORK/out.txt")
[ "$bad" -eq 0 ] || fail "$bad responses were not HTTP/3 404"

conns=$(awk '{ if ($3 > 0) n++ } END { print n + 0 }' "$WORK/out.txt")
[ "$conns" -eq 1 ] || fail "expected 1 QUIC connection, curl opened $conns"

echo "OK"
