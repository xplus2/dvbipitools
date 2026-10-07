#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

command -v nc >/dev/null 2>&1 || skip "'nc' not found on PATH"

PORT=$(free_tcp_port)

timeout 5 "$BIN" -S "$WORK/no/such/dir/m.sock" -l "127.0.0.1:$PORT" >"$WORK/uds.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "unbindable socket path: expected exit 1, got $rc (see $WORK/uds.log)"
log_has "$WORK/uds.log" "bind $WORK/no/such/dir/m.sock failed" || fail "no bind failure message (see $WORK/uds.log)"
port_open "$PORT" && fail "port $PORT is open after a failed start"

"$BIN" -S "$WORK/first.sock" -l "127.0.0.1:$PORT" >"$WORK/first.log" 2>&1 &
FIRST=$!
trap 'kill $FIRST 2>/dev/null; rm -rf "$WORK"' EXIT
wait_until 5 port_open "$PORT" || fail "first instance never listened on $PORT (see $WORK/first.log)"

timeout 5 "$BIN" -S "$WORK/second.sock" -l "127.0.0.1:$PORT" >"$WORK/second.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "port in use: expected exit 1, got $rc (see $WORK/second.log)"
log_has "$WORK/second.log" "bind 127.0.0.1:$PORT failed" || fail "no port bind failure message (see $WORK/second.log)"
[ ! -e "$WORK/second.sock" ] || fail "second instance left its socket file behind"
[ -S "$WORK/first.sock" ] || fail "first instance lost its socket"

echo "OK"

#EOF
