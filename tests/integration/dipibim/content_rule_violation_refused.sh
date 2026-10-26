#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

# TS 102 323 clause 8: ServiceURL of a service is a DVB locator, programId a CRID, publisher <domain>/<name>
check_refused() {
    label=$1
    from=$2
    to=$3
    sed "s|$from|$to|" "$WORK/in.xml" > "$WORK/bad.xml"
    printf 'keep' > "$WORK/bad.bim"
    run_expect_rc 1 "$label" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/bad.xml" -o "$WORK/bad.bim" 2> "$WORK/enc.err"
    assert_contains "$WORK/enc.err" "BE400318" "$label code"
    [ "$(cat "$WORK/bad.bim")" = "keep" ] || fail "$label: clobbered output"
}

check_refused "ServiceURL not a DVB locator" 'dvb://1.2.3' 'rtp://239.1.1.1:5000'
check_refused "programId not a CRID" 'crid://example.com/a' 'urn:a'
check_refused "publisher without readable name" 'publisher="tvlistings.tv/TV%20Listings"' 'publisher="tvlistings"'

# other profiles do not apply clause 8
run_expect_rc 0 "generic encode" "$BIN" -f xml -s "$WORK/sch" --profile generic -i "$WORK/bad.xml" -o "$WORK/generic.bim"

echo "OK"

#EOF
