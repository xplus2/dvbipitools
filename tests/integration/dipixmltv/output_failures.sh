#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<tv>\n  <channel id="channel1"><display-name>Channel One</display-name></channel>\n'
    i=0
    while [ "$i" -lt 300 ]; do
        printf '  <programme start="202012151200%02d +0000" stop="202012151300%02d +0000" channel="channel1"><title>Programme number %d with a reasonably long title</title><desc>Description of programme %d, long enough to push the output past any stdio buffer.</desc></programme>\n' "$((i % 60))" "$((i % 60))" "$i" "$i"
        i=$((i + 1))
    done
    printf '</tv>\n'
} > "$WORK/big.xmltv"
printf 'channel1,rtp://239.1.1.1:5000,1,2,101\n' > "$WORK/map.csv"

run_expect_rc 0 "convert big" "$BIN" -f xmltv -M "$WORK/map.csv" -i "$WORK/big.xmltv" -o "$WORK/big.tva.xml"
[ "$(wc -c < "$WORK/big.tva.xml")" -gt 16384 ] || fail "converted document too small to exercise stdio buffering"

run_expect_rc 1 "unwritable output xmltv to tva" "$BIN" -f xmltv -M "$WORK/map.csv" -i "$WORK/big.xmltv" -o "$WORK/no-such-dir/out.xml" 2> "$WORK/open.err"
assert_contains "$WORK/open.err" "cannot open $WORK/no-such-dir/out.xml" "unwritable output message"

run_expect_rc 1 "unwritable output tva to xmltv" "$BIN" -f tva -i "$WORK/big.tva.xml" -o "$WORK/no-such-dir/out.xmltv" 2> "$WORK/open2.err"
assert_contains "$WORK/open2.err" "cannot open" "unwritable output message tva"

[ -w /dev/full ] || skip "/dev/full not writable"
run_expect_rc 1 "full device xmltv to tva" "$BIN" -f xmltv -M "$WORK/map.csv" -i "$WORK/big.xmltv" -o /dev/full 2> "$WORK/full1.err"
assert_contains "$WORK/full1.err" "error writing /dev/full" "write failure message xmltv to tva"
run_expect_rc 1 "full device tva to xmltv" "$BIN" -f tva -i "$WORK/big.tva.xml" -o /dev/full 2> "$WORK/full2.err"
assert_contains "$WORK/full2.err" "error writing /dev/full" "write failure message tva to xmltv"

echo "OK"

#EOF
