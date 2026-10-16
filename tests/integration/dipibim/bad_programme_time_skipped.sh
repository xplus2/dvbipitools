#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"

prog() {
    printf '<ProgramInformation programId="crid://dipixmltv.invalid/channel1/%s"><BasicDescription><Title>T %s</Title></BasicDescription></ProgramInformation>\n' "$1" "$1"
}
ev() {
    printf '<ScheduleEvent><Program crid="crid://dipixmltv.invalid/channel1/%s"/><PublishedStartTime>%s</PublishedStartTime><PublishedEndTime>%s</PublishedEndTime></ScheduleEvent>\n' "$1" "$2" "$3"
}
{
    printf '<?xml version="1.0" encoding="UTF-8"?>\n<TVAMain xmlns="urn:tva:metadata:2004">\n<ProgramDescription>\n<ProgramInformationTable>\n'
    prog 20201215120000
    prog 20201215130000
    prog 20201215140000
    prog 20201215150000
    printf '</ProgramInformationTable>\n<ProgramLocationTable>\n<Schedule serviceIDRef="channel1">\n'
    ev 20201215120000 2020-12-15T12:00:00Z 2020-12-15T13:00:00Z
    ev 20201215130000 garbage 2020-12-15T14:00:00Z
    ev 20201215140000 1850-01-01T00:00:00Z 2020-12-15T15:00:00Z
    ev 20201215150000 2020-12-15T15:00:00Z not-a-time
    printf '</Schedule>\n</ProgramLocationTable>\n<ServiceInformationTable>\n<ServiceInformation serviceId="channel1"><Name>Channel One</Name><ServiceURL name="IPTV">rtp://239.1.1.1:5000</ServiceURL></ServiceInformation>\n</ServiceInformationTable>\n</ProgramDescription>\n</TVAMain>\n'
} > "$WORK/in.xml"

run_expect_rc 0 "encode skips bad programmes" "$BIN" -f xml -i "$WORK/in.xml" -o "$WORK/out.bim" 2> "$WORK/enc.err"
assert_contains "$WORK/enc.err" "BW400231" "warning code"
assert_contains "$WORK/enc.err" "channel channel1" "warning names channel"
assert_contains "$WORK/enc.err" 'start "garbage"' "warning names bad start"
assert_contains "$WORK/enc.err" 'start "1850-01-01T00:00:00Z"' "warning names out of range start"
assert_contains "$WORK/enc.err" 'end "not-a-time"' "warning names bad end"
assert_not_contains "$WORK/enc.err" "BE400102" "no generic failure"
run_expect_rc 0 "decode" "$BIN" -f bim -i "$WORK/out.bim" -o "$WORK/out.xml"
assert_contains "$WORK/out.xml" "2020-12-15T12:00:00Z" "good programme kept"
assert_not_contains "$WORK/out.xml" "T 20201215130000" "bad programme dropped"
echo "OK"

#EOF
