#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

run_expect_rc 1 "missing input" "$BIN" -f tva -i "$WORK/does-not-exist.xml" -o "$WORK/out.xmltv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "cannot open" "missing input"

echo "OK"

#EOF