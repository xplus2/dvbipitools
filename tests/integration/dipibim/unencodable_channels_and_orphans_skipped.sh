#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

prog() {
    printf '<ProgramInformation programId="crid://dipixmltv.invalid/%s/1"><BasicDescription><Title>T %s</Title></BasicDescription></ProgramInformation>\n' "$1" "$1"
}
ev() {
    printf '<ScheduleEvent><Program crid="crid://dipixmltv.invalid/%s/1"/><PublishedStartTime>2020-12-15T12:00:00Z</PublishedStartTime><PublishedEndTime>2020-12-15T13:00:00Z</PublishedEndTime></ScheduleEvent>\n' "$1"
}
{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<TVAMain xmlns="urn:tva:metadata:2004">\n<ProgramDescription>\n<ProgramInformationTable>\n'
    prog good
    prog plain
    prog orphan
    printf '</ProgramInformationTable>\n<ProgramLocationTable>\n'
    for c in good plain orphan; do
        printf '<Schedule serviceIDRef="%s">\n' "$c"
        ev $c
        printf '</Schedule>\n'
    done
    printf '</ProgramLocationTable>\n<ServiceInformationTable>\n'
    printf '<ServiceInformation serviceId="good"><Name>Good</Name><ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation>\n'
    printf '<ServiceInformation serviceId="plain"><Name>Plain</Name><ServiceURL>rtp://239.1.1.2:5000</ServiceURL></ServiceInformation>\n'
    printf '</ServiceInformationTable>\n</ProgramDescription>\n</TVAMain>\n'
} > "$WORK/in.xml"

run_expect_rc 0 "encode" "$BIN" -v -f xml -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/enc.err"
assert_contains "$WORK/enc.err" "BW400232" "channel skip warning"
assert_contains "$WORK/enc.err" "channel plain" "names skipped channel"
assert_contains "$WORK/enc.err" "BW400233" "orphan skip warning"
assert_contains "$WORK/enc.err" "channel orphan" "names orphan channel"
assert_contains "$WORK/enc.err" "1 channels, 1 programmes" "verbose counts encoded items"
run_expect_rc 0 "decode" "$BIN" -f bim -i "$WORK/out.bim" -o "$WORK/out.xml"
assert_contains "$WORK/out.xml" "T good" "good programme kept"
assert_not_contains "$WORK/out.xml" "T plain" "plain channel programme dropped"
assert_not_contains "$WORK/out.xml" "T orphan" "orphan programme dropped"

sed 's|<ServiceURL name="IPTV">|<ServiceURL>|' "$WORK/in.xml" > "$WORK/none.xml"
run_expect_rc 1 "nothing encodable" "$BIN" -f xml -i "$WORK/none.xml" -o "$WORK/none.bim" 2> "$WORK/none.err"
assert_contains "$WORK/none.err" "BE400107" "nothing to encode error"
assert_not_contains "$WORK/none.err" "BE400102" "no generic failure"
echo "OK"

#EOF
