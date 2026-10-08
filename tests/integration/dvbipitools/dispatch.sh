#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

run_expect_rc 0 "full name" "$BIN" dipibim -h > "$WORK/full.err" 2>&1
assert_contains "$WORK/full.err" "usage: dipibim" "full name reaches applet"
run_expect_rc 0 "short name" "$BIN" bim -h > "$WORK/short.err" 2>&1
assert_contains "$WORK/short.err" "usage: dipibim" "short name reaches applet"

ln -s "$BIN" "$WORK/dipibim" || skip "cannot create symlink"
run_expect_rc 0 "invoked name" "$WORK/dipibim" -h > "$WORK/linked.err" 2>&1
assert_contains "$WORK/linked.err" "usage: dipibim" "invoked name reaches applet"
echo "OK"

#EOF
