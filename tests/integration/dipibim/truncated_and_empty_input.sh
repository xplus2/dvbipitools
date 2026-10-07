#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

cat > "$WORK/in.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<TVAMain xmlns="urn:tva:metadata:2004">
<ProgramDescription>
<ProgramInformationTable>
<ProgramInformation programId="crid://dipixmltv.invalid/channel1/20201215120000"><BasicDescription><Title>News</Title><Synopsis>Evening news</Synopsis></BasicDescription></ProgramInformation>
</ProgramInformationTable>
<ProgramLocationTable>
<Schedule serviceIDRef="channel1">
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel1/20201215120000"/><PublishedStartTime>2020-12-15T12:00:00Z</PublishedStartTime><PublishedEndTime>2020-12-15T12:30:00Z</PublishedEndTime></ScheduleEvent>
</Schedule>
</ProgramLocationTable>
<ServiceInformationTable>
<ServiceInformation serviceId="channel1"><Name>Channel One</Name><ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation>
</ServiceInformationTable>
</ProgramDescription>
</TVAMain>
XML

run_expect_rc 0 "encode" "$BIN" -f xml -i "$WORK/in.xml" -o "$WORK/good.bim"
size=$(wc -c < "$WORK/good.bim")
[ "$size" -gt 16 ] || fail "encoded document implausibly small ($size bytes)"

: > "$WORK/empty.bim"
run_expect_rc 1 "empty bim" "$BIN" -f bim -i "$WORK/empty.bim" -o "$WORK/empty.xml"

: > "$WORK/empty.xml"
run_expect_rc 0 "empty xml" "$BIN" -f xml -i "$WORK/empty.xml" -o "$WORK/empty.out.bim"
run_expect_rc 0 "empty xml round trip" "$BIN" -f bim -i "$WORK/empty.out.bim" -o "$WORK/empty.back.xml"

cut=1
while [ "$cut" -lt "$size" ]; do
    head -c "$cut" "$WORK/good.bim" > "$WORK/cut.bim"
    "$BIN" -f bim -i "$WORK/cut.bim" -o "$WORK/cut.xml" >/dev/null 2>&1
    rc=$?
    [ "$rc" = 0 ] || [ "$rc" = 1 ] || fail "truncation at $cut bytes: unexpected exit $rc"
    cut=$((cut + 1))
done

echo "OK"

#EOF
