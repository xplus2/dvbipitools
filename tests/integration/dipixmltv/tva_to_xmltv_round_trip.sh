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
<ProgramInformation programId="crid://dipixmltv.invalid/orf1/20201215120000"><BasicDescription><Title>News</Title><Synopsis>Evening news</Synopsis></BasicDescription></ProgramInformation>
</ProgramInformationTable>
<GroupInformationTable/>
<ProgramLocationTable>
<Schedule serviceIDRef="orf1">
<ScheduleEvent><Program crid="crid://dipixmltv.invalid/orf1/20201215120000"/><PublishedStartTime>2020-12-15T12:00:00Z</PublishedStartTime></ScheduleEvent>
</Schedule>
</ProgramLocationTable>
<ServiceInformationTable>
<ServiceInformation serviceId="orf1">
<Name>ORFeins</Name>
<ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL>
<ServiceURL name="DTT">dvb://2.1.101</ServiceURL>
</ServiceInformation>
</ServiceInformationTable>
<CreditsInformationTable/><ProgramReviewTable/>
<SegmentInformationTable><SegmentList/><SegmentGroupList/></SegmentInformationTable>
<PurchaseInformationTable/>
</ProgramDescription>
</TVAMain>
EOF

run_expect_rc 0 "tva->xmltv" "$BIN" -f tva -i "$WORK/in.tva.xml" -o "$WORK/out.xmltv"
assert_contains "$WORK/out.xmltv" "ORFeins" "tva->xmltv"
assert_contains "$WORK/out.xmltv" "News" "tva->xmltv"
assert_contains "$WORK/out.xmltv" "20201215120000" "tva->xmltv"

echo "OK"

#EOF