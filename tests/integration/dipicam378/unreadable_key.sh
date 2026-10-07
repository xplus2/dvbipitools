#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

PORT=$(free_tcp_port)

expect_key_failure() {
    label=$1
    key=$2
    timeout 5 "$BIN" -k "$key" -s e2e-01 -p "$PORT" >"$WORK/$label.log" 2>&1
    rc=$?
    [ "$rc" = "1" ] || fail "$label: expected exit 1, got $rc (see $WORK/$label.log)"
    log_has "$WORK/$label.log" "cannot load RSA private key from -k $key" || fail "$label: no key load message (see $WORK/$label.log)"
}

expect_key_failure missing "$WORK/no-such.key"

printf 'this is not a pem file\n' >"$WORK/garbage.key"
expect_key_failure garbage "$WORK/garbage.key"

: >"$WORK/empty.key"
expect_key_failure empty "$WORK/empty.key"

if [ "$(id -u)" != "0" ]; then
    printf 'x\n' >"$WORK/locked.key"
    chmod 000 "$WORK/locked.key"
    expect_key_failure locked "$WORK/locked.key"
fi

port_open "$PORT" && fail "port $PORT is open after the failed starts"

echo "OK"

#EOF
