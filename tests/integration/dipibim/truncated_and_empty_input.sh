#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

run_expect_rc 0 "encode" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/in.xml" -o "$WORK/good.bim"
size=$(wc -c < "$WORK/good.bim")
[ "$size" -gt 16 ] || fail "encoded document implausibly small ($size bytes)"

: > "$WORK/empty.bim"
run_expect_rc 1 "empty bim" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/empty.bim" -o "$WORK/empty.xml"

: > "$WORK/empty.xml"
run_expect_rc 1 "empty xml" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/empty.xml" -o "$WORK/empty.out.bim"

for p in dvb-tva tva generic; do
    "$BIN" -f xml -s "$WORK/sch" --profile "$p" -i "$WORK/in.xml" -o "$WORK/good.$p.bim" 2>/dev/null || continue
    size=$(wc -c < "$WORK/good.$p.bim")
    cut=1
    while [ "$cut" -lt "$size" ]; do
        head -c "$cut" "$WORK/good.$p.bim" > "$WORK/cut.bim"
        "$BIN" -f bim -s "$WORK/sch" --profile "$p" -i "$WORK/cut.bim" -o "$WORK/cut.xml" >/dev/null 2>&1
        rc=$?
        [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "$p truncation at $cut bytes: unexpected exit $rc"
        cut=$((cut + 1))
    done
done

echo "OK"

#EOF
