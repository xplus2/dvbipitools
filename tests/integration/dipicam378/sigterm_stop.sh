#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

for t in openssl nc; do
    command -v "$t" >/dev/null 2>&1 || skip "'$t' not found on PATH"
done

KEY="$WORK/device.key"
openssl genrsa -out "$KEY" 1024 >"$WORK/openssl.log" 2>&1 || fail "openssl genrsa failed, see $WORK/openssl.log"

stop_cleanly() {
    label=$1
    shift
    port=$(free_tcp_port)
    "$BIN" -k "$KEY" -s e2e-01 -p "$port" "$@" >"$WORK/$label.log" 2>&1 &
    pid=$!
    wait_until 5 port_open "$port" || fail "$label: never listened on $port (see $WORK/$label.log)"
    kill -TERM "$pid"
    wait_until 5 sh -c "! kill -0 $pid 2>/dev/null" || fail "$label: still running 5s after SIGTERM"
    wait "$pid"
    rc=$?
    [ "$rc" = "0" ] || fail "$label: expected exit 0 after SIGTERM, got $rc (see $WORK/$label.log)"
    port_open "$port" && fail "$label: port $port still open after exit"
}

stop_cleanly plain
stop_cleanly metrics --metrics "$WORK/metrics.sock" --metrics-id cam1 --metrics-interval 1

echo "OK"

#EOF
