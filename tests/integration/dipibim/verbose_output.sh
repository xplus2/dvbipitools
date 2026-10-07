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
<ProgramInformation programId="crid://dipixmltv.invalid/channel1/20201215120000"><BasicDescription><Title>News</Title></BasicDescription></ProgramInformation>
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

run_expect_rc 0 "quiet encode" "$BIN" -f xml -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/quiet.err"
assert_not_contains "$WORK/quiet.err" "programmes" "quiet encode prints no summary"

run_expect_rc 0 "verbose encode" "$BIN" -v -f xml -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/enc.err"
assert_contains "$WORK/enc.err" "1 channels, 1 programmes" "verbose encode summary"
assert_contains "$WORK/enc.err" "fragments" "verbose encode fragments"

run_expect_rc 0 "verbose decode" "$BIN" -v -f bim -i "$WORK/out.bim" -o "$WORK/out.xml" 2> "$WORK/dec.err"
assert_contains "$WORK/dec.err" "1 channels, 1 programmes" "verbose decode summary"
assert_contains "$WORK/dec.err" "fragments read" "verbose decode fragments"

echo "OK"

#EOF
