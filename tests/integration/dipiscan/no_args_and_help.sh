#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 0 "help" "$BIN" --help >"$WORK/help.out" 2>"$WORK/help.err"
assert_contains "$WORK/help.out" "usage" "help usage text"
assert_contains "$WORK/help.out" "--http-proxy" "help lists options"

run_expect_rc 0 "short help" "$BIN" -h >"$WORK/h.out" 2>"$WORK/h.err"
assert_contains "$WORK/h.out" "usage" "short help usage text"

[ -r /etc/dvbipitools/dipiscan.yaml ] && skip "default config present, no-arg run would scan"
run_expect_rc 0 "no args" "$BIN" >"$WORK/noargs.out" 2>"$WORK/noargs.err"
assert_contains "$WORK/noargs.err" "--help" "no args hints at --help"

echo "OK"

#EOF
