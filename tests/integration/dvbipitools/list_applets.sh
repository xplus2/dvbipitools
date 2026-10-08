#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
. "$(dirname "$0")/../common.sh"

run_expect_rc 0 "list" "$BIN" --list > "$WORK/list.out"
for name in dipibcg dipibim dipifccret dipimetrics dipiradiohead dipirec dipiscan dipisds dipitvhead dipixmltv dipixy; do
    assert_contains "$WORK/list.out" "^$name\$" "applet $name listed"
done
run_expect_rc 2 "list with extra argument" "$BIN" --list extra 2> "$WORK/extra.err"
assert_contains "$WORK/extra.err" "usage:" "list usage message"
echo "OK"

#EOF
