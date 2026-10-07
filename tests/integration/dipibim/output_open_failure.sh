#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

printf '<TVAMain/>\n' > "$WORK/in.xml"

run_expect_rc 1 "unwritable output" "$BIN" -f xml -i "$WORK/in.xml" -o "$WORK/no-such-dir/out.bim" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "cannot open $WORK/no-such-dir/out.bim" "unwritable output"

echo "OK"

#EOF
