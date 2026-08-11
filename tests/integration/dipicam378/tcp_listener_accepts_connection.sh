#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in openssl nc; do
    command -v "$t" >/dev/null 2>&1 || fail "required tool '$t' not found on PATH"
done

PORT=27599
KEY="$WORK/device.key"

openssl genrsa -out "$KEY" 1024 >"$WORK/openssl.log" 2>&1 || fail "openssl genrsa failed, see $WORK/openssl.log"

timeout 8 "$BIN" -k "$KEY" -s e2e-01 -p $PORT -a testuser:testpass -v >"$WORK/dipicam378.log" 2>&1 &
PID=$!

i=0
while [ $i -lt 30 ]; do
    nc -z 127.0.0.1 $PORT >/dev/null 2>&1 && break
    i=$((i + 1))
    sleep 0.1
done
nc -z 127.0.0.1 $PORT >/dev/null 2>&1 || fail "server never accepted a TCP connection on port $PORT (see $WORK/dipicam378.log)"

assert_contains "$WORK/dipicam378.log" "listening on port $PORT" "startup log line"

kill $PID 2>/dev/null
wait $PID 2>/dev/null
rc=$?
[ "$rc" = 0 ] || [ "$rc" = 143 ] || fail "expected clean shutdown on SIGTERM, got exit $rc"

echo "OK"

#EOF
