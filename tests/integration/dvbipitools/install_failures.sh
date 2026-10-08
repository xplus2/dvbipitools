#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

deep="$WORK/deep"
mkdir "$deep" || fail "cannot create directory"
(
    cd "$deep" || exit 1
    while [ "$(pwd | wc -c)" -lt 3980 ]; do
        seg=$(printf '%0200d' 0)
        mkdir "$seg" && cd "$seg" || exit 1
    done
) || skip "cannot build a deep directory"
longdir=$(cd "$deep" && while [ -n "$(ls)" ]; do cd "$(ls)" || exit 1; done; pwd)
pad=$((4090 - ${#longdir}))
[ "$pad" -gt 1 ] || skip "directory already too long"
tail=$(printf '%0*d' "$((pad - 1))" 0)
mkdir "$longdir/$tail" 2> /dev/null || skip "cannot reach PATH_MAX"
run_expect_rc 1 "path too long" "$BIN" --install "$longdir/$tail" 2> "$WORK/long.err"
assert_contains "$WORK/long.err" "path too long" "path too long message"

if [ "$(id -u)" != 0 ]; then
    mkdir "$WORK/readonly" && chmod 555 "$WORK/readonly" || fail "cannot create read-only directory"
    run_expect_rc 1 "unwritable directory" "$BIN" --install "$WORK/readonly" 2> "$WORK/ro.err"
    assert_contains "$WORK/ro.err" "dvbipitools:" "unwritable directory message"
fi

shm=/dev/shm
[ -d "$shm" ] && [ -w "$shm" ] || skip "no writable /dev/shm"
dev_bin=$(stat -c %d "$BIN" 2> /dev/null) || skip "stat -c unsupported"
dev_shm=$(stat -c %d "$shm")
[ "$dev_bin" != "$dev_shm" ] || skip "binary and /dev/shm share a filesystem"
cross=$(mktemp -d "$shm/dvbipitools.XXXXXX") || skip "cannot create directory in /dev/shm"
"$BIN" --install -h "$cross" 2> "$WORK/xdev.err"
rc=$?
rm -rf "$cross"
[ "$rc" = 1 ] || fail "cross-device hardlink: expected exit 1, got $rc"
assert_contains "$WORK/xdev.err" "dvbipitools:" "cross-device message"
echo "OK"

#EOF
