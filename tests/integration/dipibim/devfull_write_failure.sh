#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

[ -w /dev/full ] || skip "/dev/full not writable"

{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<TVAMain xmlns="urn:tva:metadata:2004">\n<ProgramDescription>\n<ProgramInformationTable>\n'
    i=0
    while [ "$i" -lt 300 ]; do
        printf '<ProgramInformation programId="crid://example.com/%04d"><Title>Programme number %d with a reasonably long title, long enough to make the document larger than any stdio buffer</Title></ProgramInformation>\n' "$i" "$i"
        i=$((i + 1))
    done
    printf '</ProgramInformationTable>\n</ProgramDescription>\n</TVAMain>\n'
} > "$WORK/big.xml"

run_expect_rc 0 "encode big" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/big.xml" -o "$WORK/big.bim"
run_expect_rc 1 "encode to full device" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/big.xml" -o /dev/full 2> "$WORK/enc.err"
run_expect_rc 0 "decode big" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/big.bim" -o "$WORK/big.out.xml"
[ "$(wc -c < "$WORK/big.out.xml")" -gt 16384 ] || fail "decoded document too small to exercise stdio buffering"
run_expect_rc 1 "decode to full device" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/big.bim" -o /dev/full 2> "$WORK/dec.err"
assert_contains "$WORK/dec.err" "error writing /dev/full" "decode write failure message"
run_expect_rc 1 "encode to stdout on full device" sh -c '"$0" -f xml -s "$3" -i "$1" -o - > /dev/full 2> "$2"' "$BIN" "$WORK/big.xml" "$WORK/enc-stdout.err" "$WORK/sch"
assert_contains "$WORK/enc-stdout.err" "error writing" "encode stdout write failure message"
run_expect_rc 1 "decode to stdout on full device" sh -c '"$0" -f bim -s "$3" -i "$1" -o - > /dev/full 2> "$2"' "$BIN" "$WORK/big.bim" "$WORK/dec-stdout.err" "$WORK/sch"
assert_contains "$WORK/dec-stdout.err" "error writing" "decode stdout write failure message"
echo "OK"

#EOF
