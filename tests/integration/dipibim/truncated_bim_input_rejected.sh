#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

# too short: decode_bim_to_xml requires at least 4 length-prefix bytes
printf 'ab' > "$WORK/too_short.bim"
run_expect_rc 1 "too short" "$BIN" -f bim -i "$WORK/too_short.bim" -o "$WORK/out1.xml"

# length prefix claims far more bits than the file actually has
printf '\377\377\377\377tiny' > "$WORK/bad_length.bim"
run_expect_rc 1 "bad length prefix" "$BIN" -f bim -i "$WORK/bad_length.bim" -o "$WORK/out2.xml"

# failed conversion must leave an existing output untouched
printf 'keep' > "$WORK/keep.xml"
run_expect_rc 1 "failed decode keeps output" "$BIN" -f bim -i "$WORK/too_short.bim" -o "$WORK/keep.xml"
[ "$(cat "$WORK/keep.xml")" = "keep" ] || fail "failed conversion clobbered output"

# -i and -o naming same file must work
printf '<TVAMain><ServiceInformation serviceId="c1"><Name>One</Name><ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation></TVAMain>\n' > "$WORK/same.xml"
run_expect_rc 0 "encode in place" "$BIN" -f xml -i "$WORK/same.xml" -o "$WORK/same.xml"
[ -s "$WORK/same.xml" ] || fail "in-place conversion left empty output"

echo "OK"
