#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

# element outside schema
sed 's|<ProgramDescription>|<ProgramDescription><Bogus/>|' "$WORK/in.xml" > "$WORK/bogus.xml"
run_expect_rc 1 "element outside the schema" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/bogus.xml" -o "$WORK/bogus.bim" 2> "$WORK/bogus.err"
assert_contains "$WORK/bogus.err" "BE400315" "element outside the schema"

# root not coded by profile
run_expect_rc 1 "root without fragment path" "$BIN" -f xml -s "$WORK/sd" -i "$WORK/sd.xml" -o "$WORK/root.bim" 2> "$WORK/root.err"
assert_contains "$WORK/root.err" "BE400314" "root without fragment path"

# no schema file below directory, several files for 1 namespace
mkdir "$WORK/empty" "$WORK/dup"
run_expect_rc 1 "no schema" "$BIN" -f xml -s "$WORK/empty" -i "$WORK/in.xml" -o "$WORK/none.bim" 2> "$WORK/none.err"
assert_contains "$WORK/none.err" "BE000754" "no schema"
cp "$WORK/sch/tva.xsd" "$WORK/dup/a.xsd"
cp "$WORK/sch/tva.xsd" "$WORK/dup/b.xsd"
run_expect_rc 1 "ambiguous schema" "$BIN" -f xml -s "$WORK/dup" -i "$WORK/in.xml" -o "$WORK/dup.bim" 2> "$WORK/dup.err"
assert_contains "$WORK/dup.err" "BE000755" "ambiguous schema"
run_expect_rc 0 "ambiguity resolved by the file" "$BIN" -f xml -s "$WORK/dup" -s "$WORK/dup/a.xsd" -i "$WORK/in.xml" -o "$WORK/dup.bim"

run_expect_rc 1 "two schema files" "$BIN" -f xml -s "$WORK/dup/a.xsd" -s "$WORK/dup/b.xsd" -i "$WORK/in.xml" -o "$WORK/two.bim" 2> "$WORK/two.err"
assert_contains "$WORK/two.err" "BS000752" "two schema files"

echo "OK"

#EOF
