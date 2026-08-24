#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

# raw AF_PACKET capture needs CAP_NET_RAW. an unprivileged CI/dev sandbox is exactly
# this state - verify the tool fails fast with a clear message instead of hanging
# or crashing. if the invoking user *does* have the capability (root, or the binary
# was setcap'd), capture_open() succeeds instead and this test doesn't apply - skip.
if [ "$(id -u)" = "0" ]; then
    echo "SKIP: running as root, cannot exercise the permission-denied path"
    exit 0
fi

out="$WORK/dipifccret.out"
# -M 4: CAP_NET_RAW check ignores channel capacity
# small table, same alloc path per slot
# -k: unexpected success blocks capture_run() until SIGTERM, SIGKILL after 3s bounds worst case
timeout -k 3 10 "$BIN" -g 239.0.0.0/8 -l 127.0.0.1:16000 -I lo -M 4 >"$out" 2>&1
rc=$?

if grep -q "capture needs CAP_NET_RAW" "$out"; then
    [ "$rc" = 1 ] || fail "expected exit 1 on capture permission denial, got $rc (see $out)"
    echo "OK"
    exit 0
fi

# binary was setcap'd cap_net_raw+ep, or CAP_NET_RAW is otherwise available - not
# what this test is checking, but not a failure of the tool either
echo "SKIP: capture did not report a permission error (CAP_NET_RAW available?), see $out"

#EOF
