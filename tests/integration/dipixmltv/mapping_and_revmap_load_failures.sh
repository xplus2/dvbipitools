#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

printf '<tv></tv>\n' > "$WORK/in.xmltv"
printf '<TVAMain/>\n' > "$WORK/in.tva.xml"

run_expect_rc 1 "missing mapping file" "$BIN" -f xmltv -M "$WORK/no-such-map.csv" -i "$WORK/in.xmltv" -o "$WORK/out.xml" 2> "$WORK/missing.err"
assert_contains "$WORK/missing.err" "mapping: cannot open" "missing mapping message"

printf 'channel1,rtp://239.1.1.1:5000,notanumber,2,101\n' > "$WORK/badnum.csv"
run_expect_rc 1 "bad numbers in mapping" "$BIN" -f xmltv -M "$WORK/badnum.csv" -i "$WORK/in.xmltv" -o "$WORK/out.xml" 2> "$WORK/badnum.err"
assert_contains "$WORK/badnum.err" "mapping: line 1: bad tsid/onid/sid" "bad numbers message"

printf '# comment\n\nchannel1,rtp://239.1.1.1:5000,1,2,70000\n' > "$WORK/range.csv"
run_expect_rc 1 "out of range sid in mapping" "$BIN" -f xmltv -M "$WORK/range.csv" -i "$WORK/in.xmltv" -o "$WORK/out.xml" 2> "$WORK/range.err"
assert_contains "$WORK/range.err" "mapping: line 3: bad tsid/onid/sid" "out of range message"

printf 'channel1,rtp://239.1.1.1:5000\n' > "$WORK/short.csv"
run_expect_rc 1 "too few fields in mapping" "$BIN" -f xmltv -M "$WORK/short.csv" -i "$WORK/in.xmltv" -o "$WORK/out.xml" 2> "$WORK/short.err"
assert_contains "$WORK/short.err" "mapping: line 1: expected id,uri,tsid,onid,sid" "too few fields message"

run_expect_rc 1 "missing reverse map" "$BIN" -f tva -R "$WORK/no-such-revmap.csv" -i "$WORK/in.tva.xml" -o "$WORK/out.xmltv" 2> "$WORK/rev.err"
assert_contains "$WORK/rev.err" "cannot open reverse map file" "missing reverse map message"

printf 'no-comma-on-this-line\n' > "$WORK/badrev.csv"
run_expect_rc 1 "malformed reverse map" "$BIN" -f tva -R "$WORK/badrev.csv" -i "$WORK/in.tva.xml" -o "$WORK/out.xmltv" 2> "$WORK/badrev.err"
assert_contains "$WORK/badrev.err" "reverse map line 1: expected uri,id" "malformed reverse map message"

echo "OK"

#EOF
