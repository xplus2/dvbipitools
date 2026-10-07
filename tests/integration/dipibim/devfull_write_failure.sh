#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

[ -w /dev/full ] || skip "/dev/full not writable"

{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<TVAMain xmlns="urn:tva:metadata:2004">\n<ProgramDescription>\n<ProgramInformationTable>\n'
    i=0
    while [ "$i" -lt 300 ]; do
        printf '<ProgramInformation programId="crid://dipixmltv.invalid/channel1/2020121512%04d"><BasicDescription><Title>Programme number %d with a reasonably long title</Title><Synopsis>Synopsis text for programme %d, long enough to make the document larger than any stdio buffer.</Synopsis></BasicDescription></ProgramInformation>\n' "$i" "$i" "$i"
        i=$((i + 1))
    done
    printf '</ProgramInformationTable>\n<ProgramLocationTable>\n<Schedule serviceIDRef="channel1">\n'
    i=0
    while [ "$i" -lt 300 ]; do
        printf '<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel1/2020121512%04d"/><PublishedStartTime>2020-12-15T12:%02d:00Z</PublishedStartTime><PublishedEndTime>2020-12-15T13:%02d:00Z</PublishedEndTime></ScheduleEvent>\n' "$i" "$((i % 60))" "$((i % 60))"
        i=$((i + 1))
    done
    printf '</Schedule>\n</ProgramLocationTable>\n<ServiceInformationTable>\n<ServiceInformation serviceId="channel1"><Name>Channel One</Name><ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation>\n</ServiceInformationTable>\n</ProgramDescription>\n</TVAMain>\n'
} > "$WORK/big.xml"

run_expect_rc 0 "encode big" "$BIN" -f xml -i "$WORK/big.xml" -o "$WORK/big.bim"
run_expect_rc 1 "encode to full device" "$BIN" -f xml -i "$WORK/big.xml" -o /dev/full 2> "$WORK/enc.err"
run_expect_rc 0 "decode big" "$BIN" -f bim -i "$WORK/big.bim" -o "$WORK/big.out.xml"
[ "$(wc -c < "$WORK/big.out.xml")" -gt 16384 ] || fail "decoded document too small to exercise stdio buffering"
run_expect_rc 1 "decode to full device" "$BIN" -f bim -i "$WORK/big.bim" -o /dev/full 2> "$WORK/dec.err"
assert_contains "$WORK/dec.err" "error writing /dev/full" "decode write failure message"
echo "OK"

#EOF
