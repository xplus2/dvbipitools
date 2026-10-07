#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 1 "missing directory" "$BIN" -m 239.250.250.1-239.250.250.1 -t 1 -o "$WORK/no-such-dir/out.m3u" >"$WORK/dir.out" 2>"$WORK/dir.err"
assert_contains "$WORK/dir.err" "open $WORK/no-such-dir/out.m3u" "open failure message"

[ -w /dev/full ] || skip "/dev/full not writable"
run_expect_rc 1 "full device" "$BIN" -m 239.250.250.1-239.250.250.1 -t 1 -o /dev/full >"$WORK/full.out" 2>"$WORK/full.err"
assert_contains "$WORK/full.err" "error writing /dev/full" "write failure message"

echo "OK"

#EOF
