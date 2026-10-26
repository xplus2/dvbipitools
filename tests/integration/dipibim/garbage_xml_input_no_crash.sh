#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

printf 'this is not xml at all, just %s garbage\n' "$(head -c 200 /dev/urandom | tr -dc 'a-zA-Z0-9')" > "$WORK/garbage.xml"

run_expect_rc 1 "encode" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/garbage.xml" -o "$WORK/out.bim" 2> "$WORK/enc.err"
assert_contains "$WORK/enc.err" "BE000756" "garbage input"

echo "OK"

#EOF
