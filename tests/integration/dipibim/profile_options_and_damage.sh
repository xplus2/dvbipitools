#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

round_trip() {
    label=$1
    shift
    run_expect_rc 0 "$label encode" "$BIN" -v -f xml -s "$WORK/sch" "$@" -i "$WORK/in.xml" -o "$WORK/rt.bim" 2> "$WORK/rt.enc.err"
    run_expect_rc 0 "$label decode" "$BIN" -v -f bim -s "$WORK/sch" "$@" -i "$WORK/rt.bim" -o "$WORK/rt.xml" 2> "$WORK/rt.dec.err"
    assert_contains "$WORK/rt.xml" "Alpha" "$label round trip"
}

round_trip "dvb-tva" --profile dvb-tva
round_trip "dvb-tva locator duration pubtime" --profile dvb-tva --dvb-locator --dvb-duration --dvb-pubtime
round_trip "tva" --profile tva
round_trip "tva zbuf" --profile tva --zbuf-size 64
round_trip "tva gather" --profile tva --zbuf-size 64 --zgather
round_trip "generic" --profile generic
round_trip "generic 15938-1" --profile generic --syntax 15938-1
round_trip "generic len-mode 1" --profile generic --len-mode 1
round_trip "generic len-mode 2" --profile generic --len-mode 2
round_trip "generic deferred cast" --profile generic --deferred --cast
round_trip "generic conformance" --profile generic --compat conformance

run_expect_rc 0 "sdns encode" "$BIN" -v -f xml -s "$WORK/sd" --profile sdns -i "$WORK/sd.xml" -o "$WORK/sd.bim" 2> "$WORK/sd.enc.err"
run_expect_rc 0 "sdns decode" "$BIN" -v -f bim -s "$WORK/sd" --profile sdns -i "$WORK/sd.bim" -o "$WORK/sd.out.xml" 2> "$WORK/sd.dec.err"
assert_contains "$WORK/sd.out.xml" "Two" "sdns round trip"

run_expect_rc 0 "dvb-tva encode for mismatch" "$BIN" -f xml -s "$WORK/sch" --profile dvb-tva -i "$WORK/in.xml" -o "$WORK/mm.dvb.bim"
run_expect_rc 0 "tva encode for mismatch" "$BIN" -f xml -s "$WORK/sch" --profile tva -i "$WORK/in.xml" -o "$WORK/mm.tva.bim"
for p in tva sdns generic; do
    "$BIN" -f bim -s "$WORK/sch" --profile "$p" -i "$WORK/mm.dvb.bim" -o "$WORK/mm.out.xml" 2> "$WORK/mm.err"
    rc=$?
    [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "decode dvb-tva stream as $p: exit $rc"
done
for p in dvb-tva sdns generic; do
    "$BIN" -f bim -s "$WORK/sch" --profile "$p" -i "$WORK/mm.tva.bim" -o "$WORK/mm.out.xml" 2> "$WORK/mm.err"
    rc=$?
    [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "decode tva stream as $p: exit $rc"
done
run_expect_rc 1 "tva document as sdns" "$BIN" -f xml -s "$WORK/sch" --profile sdns -i "$WORK/in.xml" -o "$WORK/mm.out.bim" 2> "$WORK/mm.err"
run_expect_rc 1 "sd document as tva" "$BIN" -f xml -s "$WORK/sd" --profile tva -i "$WORK/sd.xml" -o "$WORK/mm.out.bim" 2> "$WORK/mm.err"
run_expect_rc 1 "sd document as dvb-tva" "$BIN" -f xml -s "$WORK/sd" --profile dvb-tva -i "$WORK/sd.xml" -o "$WORK/mm.out.bim" 2> "$WORK/mm.err"

printf '<TVAMain/>' > "$WORK/nons.xml"
for p in dvb-tva tva sdns generic; do
    run_expect_rc 1 "$p root without namespace" "$BIN" -f xml -s "$WORK/sch" --profile "$p" -i "$WORK/nons.xml" -o "$WORK/nons.bim" 2> "$WORK/nons.err"
done
assert_contains "$WORK/nons.err" "BE400314" "root without namespace code"

printf '<Other xmlns="urn:tva:metadata:2004"/>' > "$WORK/other.xml"
for p in dvb-tva tva sdns generic; do
    "$BIN" -f xml -s "$WORK/sch" --profile "$p" -i "$WORK/other.xml" -o "$WORK/other.bim" 2> "$WORK/other.err"
    rc=$?
    [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "$p unknown root: exit $rc"
done

damage() {
    label=$1
    stream=$2
    shift 2
    size=$(wc -c < "$stream")
    cut=0
    while [ "$cut" -lt "$size" ]; do
        head -c "$cut" "$stream" > "$WORK/dmg.bim"
        "$BIN" -f bim "$@" -i "$WORK/dmg.bim" -o "$WORK/dmg.xml" >/dev/null 2>&1
        rc=$?
        [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "$label truncated at $cut: exit $rc"
        cut=$((cut + 1))
    done
    pos=0
    while [ "$pos" -lt "$size" ]; do
        cp "$stream" "$WORK/dmg.bim"
        printf '\377' | dd of="$WORK/dmg.bim" bs=1 seek="$pos" conv=notrunc 2>/dev/null
        "$BIN" -f bim "$@" -i "$WORK/dmg.bim" -o "$WORK/dmg.xml" >/dev/null 2>&1
        rc=$?
        [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "$label byte $pos damaged: exit $rc"
        cp "$stream" "$WORK/dmg.bim"
        printf '\000' | dd of="$WORK/dmg.bim" bs=1 seek="$pos" conv=notrunc 2>/dev/null
        "$BIN" -f bim "$@" -i "$WORK/dmg.bim" -o "$WORK/dmg.xml" >/dev/null 2>&1
        rc=$?
        [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "$label byte $pos zeroed: exit $rc"
        pos=$((pos + 1))
    done
}

damage "dvb-tva" "$WORK/mm.dvb.bim" -s "$WORK/sch" --profile dvb-tva
damage "tva" "$WORK/mm.tva.bim" -s "$WORK/sch" --profile tva
damage "sdns" "$WORK/sd.bim" -s "$WORK/sd" --profile sdns
run_expect_rc 0 "generic encode for damage" "$BIN" -f xml -s "$WORK/sch" --profile generic -i "$WORK/in.xml" -o "$WORK/mm.gen.bim"
damage "generic" "$WORK/mm.gen.bim" -s "$WORK/sch" --profile generic

echo "OK"

#EOF
