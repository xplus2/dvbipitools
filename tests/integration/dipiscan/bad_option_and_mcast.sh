#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 2 "unknown option" "$BIN" --no-such-option >"$WORK/opt.out" 2>"$WORK/opt.err"
assert_contains "$WORK/opt.err" "--help" "unknown option hints at --help"

run_expect_rc 2 "bad -m" "$BIN" -m bogus >"$WORK/m.out" 2>"$WORK/m.err"
assert_contains "$WORK/m.err" "invalid -m address: bogus" "bad -m message"
assert_contains "$WORK/m.err" "--help" "bad -m hints at --help"

run_expect_rc 2 "bad -p" "$BIN" -m 239.250.250.1 -p 0 >"$WORK/p.out" 2>"$WORK/p.err"
assert_contains "$WORK/p.err" "invalid -p port range" "bad -p message"

run_expect_rc 2 "xml without provider" "$BIN" -m 239.250.250.1 -f xml >"$WORK/x.out" 2>"$WORK/x.err"
assert_contains "$WORK/x.err" "missing -P provider" "missing provider message"

echo "OK"

#EOF
