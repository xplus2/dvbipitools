#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 2 "bad -f value" "$BIN" -f bogus -i "$WORK/x" -o "$WORK/y" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "invalid -f format" "bad -f value"

echo "OK"

#EOF