#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

run_expect_rc 0 "quiet encode" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/quiet.err"
assert_not_contains "$WORK/quiet.err" "fragments" "quiet encode prints no summary"

run_expect_rc 0 "verbose encode" "$BIN" -v -f xml -s "$WORK/sch" -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/enc.err"
assert_contains "$WORK/enc.err" "BD700170" "verbose encode code"
assert_contains "$WORK/enc.err" "4 fragments ->" "verbose encode fragments"

run_expect_rc 0 "verbose decode" "$BIN" -v -f bim -s "$WORK/sch" -i "$WORK/out.bim" -o "$WORK/out.xml" 2> "$WORK/dec.err"
assert_contains "$WORK/dec.err" "BD700171" "verbose decode code"
assert_contains "$WORK/dec.err" "4 fragments read" "verbose decode fragments"

echo "OK"

#EOF
