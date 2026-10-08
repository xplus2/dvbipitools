#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

FPB=$(free_port_block 3)

for uri in "rist://@127.0.0.1:$FPB" "srt://@127.0.0.1:$((FPB + 1))"; do
    timeout 3 "$BIN" -i "$uri" -o "$WORK/out.ts" -f ts >"$WORK/log" 2>&1
    rc=$?
    if [ "$rc" = 1 ] && grep -q "cannot open -i" "$WORK/log"; then
        skip "input scheme not supported in this build: $uri"
    fi
    [ "$rc" = 124 ] || fail "$uri: expected to wait for input until stopped, got exit $rc"
    assert_contains "$WORK/log" "i:$uri" "$uri accepted as input"
done
echo "OK"

#EOF
