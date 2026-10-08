#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"
APPLETS_TAG="applets:"

run_expect_rc 0 "no arguments prints help" "$BIN" 2> "$WORK/help.err"
assert_contains "$WORK/help.err" "$APPLETS_TAG" "help lists applets"
run_expect_rc 2 "unknown applet" "$BIN" nosuchapplet 2> "$WORK/unknown.err"
assert_contains "$WORK/unknown.err" "no applet named 'nosuchapplet'" "unknown applet message"
assert_contains "$WORK/unknown.err" "$APPLETS_TAG" "unknown applet shows help"

ln -s "$BIN" "$WORK/stranger" || skip "cannot create symlink"
run_expect_rc 2 "unknown invoked name" "$WORK/stranger" 2> "$WORK/stranger.err"
assert_contains "$WORK/stranger.err" "not invoked under a known name" "unknown invoked name message"

ln -s "$BIN" "$WORK/dipi"
run_expect_rc 0 "dipi alias prints help" "$WORK/dipi" 2> "$WORK/dipi.err"
assert_contains "$WORK/dipi.err" "$APPLETS_TAG" "dipi alias help"

ln -s "$BIN" "$WORK/dipidipiyeah"
run_expect_rc 0 "hidden name" "$WORK/dipidipiyeah" > "$WORK/hidden.out"
[ -s "$WORK/hidden.out" ] || fail "hidden name printed nothing"
if command -v bash > /dev/null 2>&1; then
    bash -c 'exec -a "" "$0"' "$BIN" 2> "$WORK/empty.err"
    [ "$?" = 2 ] || fail "empty argv0: expected exit 2"
    assert_contains "$WORK/empty.err" "$APPLETS_TAG" "empty argv0 shows help"
fi
echo "OK"

#EOF
