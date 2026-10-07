#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

printf '<tv><channel id="channel1"><display-name>Channel One</display-name></channel></tv>\n' > "$WORK/guide.xmltv"

run_expect_rc 1 "missing scan file" "$BIN" -S "$WORK/no-such-scan.csv" -i "$WORK/guide.xmltv" -o "$WORK/suggested.csv" 2> "$WORK/stderr"
assert_contains "$WORK/stderr" "cannot open $WORK/no-such-scan.csv" "missing scan file message"
[ ! -s "$WORK/suggested.csv" ] || fail "output written despite missing scan file"

echo "OK"

#EOF
