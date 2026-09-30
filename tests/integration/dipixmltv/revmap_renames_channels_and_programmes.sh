#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

cat > "$WORK/in.tva.xml" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<TVAMain xmlns="urn:tva:metadata:2004">
<ProgramDescription>
<MetadataOriginationInformationTable/>
<ClassificationSchemeTable/>
<ProgramInformationTable>
<ProgramInformation programId="crid://dipixmltv.invalid/channel1/20201215120000"><BasicDescription><Title>News One</Title></BasicDescription></ProgramInformation>
<ProgramInformation programId="crid://dipixmltv.invalid/channel1/20201215130000"><BasicDescription><Title>Weather One</Title></BasicDescription></ProgramInformation>
<ProgramInformation programId="crid://dipixmltv.invalid/channel2/20201215120000"><BasicDescription><Title>News Two</Title></BasicDescription></ProgramInformation>
<ProgramInformation programId="crid://dipixmltv.invalid/channel2/20201215130000"><BasicDescription><Title>Weather Two</Title></BasicDescription></ProgramInformation>
</ProgramInformationTable>
<GroupInformationTable/>
<ProgramLocationTable>
<Schedule serviceIDRef="channel1">
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel1/20201215120000"/><PublishedStartTime>2020-12-15T12:00:00Z</PublishedStartTime></ScheduleEvent>
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel1/20201215130000"/><PublishedStartTime>2020-12-15T13:00:00Z</PublishedStartTime></ScheduleEvent>
</Schedule>
<Schedule serviceIDRef="channel2">
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel2/20201215120000"/><PublishedStartTime>2020-12-15T12:00:00Z</PublishedStartTime></ScheduleEvent>
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel2/20201215130000"/><PublishedStartTime>2020-12-15T13:00:00Z</PublishedStartTime></ScheduleEvent>
</Schedule>
</ProgramLocationTable>
<ServiceInformationTable>
<ServiceInformation serviceId="channel1">
<Name>Channel One</Name>
<ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL>
</ServiceInformation>
<ServiceInformation serviceId="channel2">
<Name>Channel Two</Name>
<ServiceURL name="IPTV">rtp://239.1.1.2:5000</ServiceURL>
</ServiceInformation>
</ServiceInformationTable>
<CreditsInformationTable/><ProgramReviewTable/>
<SegmentInformationTable><SegmentList/><SegmentGroupList/></SegmentInformationTable>
<PurchaseInformationTable/>
</ProgramDescription>
</TVAMain>
EOF

cat > "$WORK/revmap.csv" <<'EOF'
rtp://239.1.1.1:5000,pref.one
rtp://239.1.1.2:5000,pref.two
EOF

run_expect_rc 0 "tva->xmltv with revmap" "$BIN" -f tva -R "$WORK/revmap.csv" -i "$WORK/in.tva.xml" -o "$WORK/out.xmltv"

assert_contains "$WORK/out.xmltv" 'channel id="pref.one"' "channel1 renamed"
assert_contains "$WORK/out.xmltv" 'channel id="pref.two"' "channel2 renamed"
assert_not_contains "$WORK/out.xmltv" 'id="channel1"' "old channel1 id gone"
assert_not_contains "$WORK/out.xmltv" 'id="channel2"' "old channel2 id gone"

assert_contains "$WORK/out.xmltv" 'channel="pref.one"' "at least one programme repointed to pref.one"
assert_contains "$WORK/out.xmltv" 'channel="pref.two"' "at least one programme repointed to pref.two"
assert_not_contains "$WORK/out.xmltv" 'channel="channel1"' "no programme left pointing at old channel1"
assert_not_contains "$WORK/out.xmltv" 'channel="channel2"' "no programme left pointing at old channel2"

PREF_ONE_COUNT=$(grep -o 'channel="pref.one"' "$WORK/out.xmltv" | wc -l)
PREF_TWO_COUNT=$(grep -o 'channel="pref.two"' "$WORK/out.xmltv" | wc -l)
[ "$PREF_ONE_COUNT" = "2" ] || fail "expected 2 programmes on pref.one, got $PREF_ONE_COUNT"
[ "$PREF_TWO_COUNT" = "2" ] || fail "expected 2 programmes on pref.two, got $PREF_TWO_COUNT"

assert_contains "$WORK/out.xmltv" "News One" "programme title preserved"
assert_contains "$WORK/out.xmltv" "Weather Two" "programme title preserved"

echo "OK"
