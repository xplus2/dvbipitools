#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

run_expect_rc 1 "unwritable output" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/in.xml" -o "$WORK/no-such-dir/out.bim" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "cannot open $WORK/no-such-dir/out.bim" "unwritable output"

echo "OK"

#EOF
