#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

mkdir "$WORK/sym" "$WORK/hard" || fail "cannot create directories"
cp "$BIN" "$WORK/dvbipitools" || fail "cannot copy binary"

"$BIN" --list > "$WORK/list.out" || fail "list failed"

run_expect_rc 0 "symlink install" "$BIN" --install "$WORK/sym"
while read -r name; do
    [ -L "$WORK/sym/$name" ] || fail "$name is not a symlink"
done < "$WORK/list.out"
run_expect_rc 0 "install over existing links" "$BIN" --install "$WORK/sym" 2> "$WORK/again.err"
assert_contains "$WORK/again.err" "exists, skipped" "existing links skipped"
run_expect_rc 0 "installed link runs" "$WORK/sym/dipibim" -h 2> /dev/null

run_expect_rc 0 "hardlink install" "$WORK/dvbipitools" --install -h "$WORK/hard"
while read -r name; do
    [ -f "$WORK/hard/$name" ] && [ ! -L "$WORK/hard/$name" ] || fail "$name is not a hard link"
done < "$WORK/list.out"

run_expect_rc 1 "missing directory" "$BIN" --install "$WORK/absent" 2> "$WORK/absent.err"
assert_contains "$WORK/absent.err" "$WORK/absent" "missing directory message"
: > "$WORK/plainfile"
run_expect_rc 1 "not a directory" "$BIN" --install "$WORK/plainfile" 2> "$WORK/plain.err"
assert_contains "$WORK/plain.err" "not a directory" "not a directory message"
run_expect_rc 2 "install without directory" "$BIN" --install 2> "$WORK/usage.err"
assert_contains "$WORK/usage.err" "usage:" "install usage message"
run_expect_rc 2 "install with unknown flag" "$BIN" --install -x "$WORK/sym" 2> /dev/null
echo "OK"

#EOF
