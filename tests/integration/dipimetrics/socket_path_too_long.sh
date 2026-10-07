#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

long="$WORK/$(printf 'd%.0s' $(seq 1 40))/$(printf 'e%.0s' $(seq 1 40))/m.sock"

timeout 5 "$BIN" -S "$long" -l "127.0.0.1:$(free_tcp_port)" >"$WORK/dipimetrics.log" 2>&1
rc=$?
[ "$rc" = "1" ] || fail "expected exit 1, got $rc (see $WORK/dipimetrics.log)"
log_has "$WORK/dipimetrics.log" "socket path too long" || fail "no 'socket path too long' message (see $WORK/dipimetrics.log)"

echo "OK"

#EOF
