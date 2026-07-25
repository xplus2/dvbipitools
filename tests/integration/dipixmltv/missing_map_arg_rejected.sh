#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

printf '<tv></tv>\n' > "$WORK/in.xmltv"

run_expect_rc 2 "no -M" "$BIN" -f xmltv -i "$WORK/in.xmltv" -o "$WORK/out.tva.xml" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "missing -M map" "no -M"

echo "OK"

#EOF