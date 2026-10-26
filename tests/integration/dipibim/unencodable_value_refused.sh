#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

# values the profile codecs cannot represent are refused, never dropped
for bad in garbage 1850-01-01T00:00:00Z; do
    sed "s|2024-05-01T10:00:00Z|$bad|" "$WORK/in.xml" > "$WORK/bad.xml"
    printf 'keep' > "$WORK/bad.bim"
    run_expect_rc 1 "encode $bad" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/bad.xml" -o "$WORK/bad.bim" 2> "$WORK/enc.err"
    assert_contains "$WORK/enc.err" "BE400315" "refusal code for $bad"
    [ "$(cat "$WORK/bad.bim")" = "keep" ] || fail "refused conversion clobbered output"
done

echo "OK"

#EOF
