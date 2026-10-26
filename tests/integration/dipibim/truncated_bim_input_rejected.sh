#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

# no DVB-TVA-init
printf 'ab' > "$WORK/no_init.bim"
run_expect_rc 1 "no init" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/no_init.bim" -o "$WORK/out0.xml" 2>"$WORK/e0"
log_has "$WORK/e0" "BE400242" || fail "no init: wrong error"

# init but less than 4 length-prefix bytes
printf "${DVB_INIT}ab" > "$WORK/too_short.bim"
run_expect_rc 1 "too short" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/too_short.bim" -o "$WORK/out1.xml" 2>"$WORK/e1"
log_has "$WORK/e1" "BE400103" || fail "too short: wrong error"

# length prefix claims far more bits than the file actually has
printf "${DVB_INIT}\377\377\377\377tiny" > "$WORK/bad_length.bim"
run_expect_rc 1 "bad length prefix" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/bad_length.bim" -o "$WORK/out2.xml" 2>"$WORK/e2"
log_has "$WORK/e2" "BE400104" || fail "bad length: wrong error"

# stream of another profile
"$BIN" -f xml -s "$WORK/sch" --profile generic -i "$WORK/in.xml" -o "$WORK/generic.bim" || fail "encode generic"
run_expect_rc 1 "generic stream as dvb-tva" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/generic.bim" -o "$WORK/out3.xml" 2>"$WORK/e3"
log_has "$WORK/e3" "BE400242" || fail "generic stream as dvb-tva: wrong error"

# failed conversion must leave an existing output untouched
printf 'keep' > "$WORK/keep.xml"
run_expect_rc 1 "failed decode keeps output" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/too_short.bim" -o "$WORK/keep.xml"
[ "$(cat "$WORK/keep.xml")" = "keep" ] || fail "failed conversion clobbered output"

# -i and -o naming same file must work
cp "$WORK/in.xml" "$WORK/same.xml"
run_expect_rc 0 "encode in place" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/same.xml" -o "$WORK/same.xml"
[ -s "$WORK/same.xml" ] || fail "in-place conversion left empty output"

echo "OK"
