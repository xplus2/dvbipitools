#!/bin/sh
# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

BIN=$1
. "$(dirname "$0")/../common.sh"
. "$(dirname "$0")/fixture.inc"
make_fixtures

for p in dvb-tva tva generic; do
    "$BIN" -f xml -s "$WORK/sch" --profile "$p" -i "$WORK/in.xml" -o "$WORK/out.$p.bim" 2>"$WORK/enc.err"
    rc=$?
    if [ "$rc" != 0 ] && log_has "$WORK/enc.err" "BE400317"; then
        continue
    fi
    [ "$rc" = 0 ] || fail "encode $p: exit $rc"
    [ -s "$WORK/out.$p.bim" ] || fail "encode $p: output empty"
    run_expect_rc 0 "decode $p" "$BIN" -f bim -s "$WORK/sch" --profile "$p" -i "$WORK/out.$p.bim" -o "$WORK/out.$p.xml"
    assert_contains "$WORK/out.$p.xml" "Channel One" "decode $p"
    assert_contains "$WORK/out.$p.xml" "Alpha" "decode $p"
    assert_contains "$WORK/out.$p.xml" "2024-05-01T10:00:00Z" "decode $p dateTime"
    assert_contains "$WORK/out.$p.xml" 'xmlns="urn:tva:metadata:2004"' "decode $p namespace"
    assert_contains "$WORK/out.$p.xml" 'publisher="tvlistings.tv/TV%20Listings"' "decode $p attribute"
    cmp -s "$WORK/out.$p.xml" "$WORK/out.dvb-tva.xml" || fail "decode $p: differs from dvb-tva"
done

# DVB-TVA-init carrying SchemaURI[0]
head -c 33 "$WORK/out.dvb-tva.bim" | grep -q "urn:tva:metadata:2004" || fail "encode: no DVB-TVA-init"

# schema file instead of a directory
run_expect_rc 0 "encode with schema file" "$BIN" -f xml -s "$WORK/sch/tva.xsd" -i "$WORK/in.xml" -o "$WORK/file.bim"
cmp -s "$WORK/file.bim" "$WORK/out.dvb-tva.bim" || fail "schema file: different output"

# SD&S segment
"$BIN" -f xml -s "$WORK/sd" --profile sdns -i "$WORK/sd.xml" -o "$WORK/sd.bim" 2>"$WORK/sd.err"
rc=$?
if [ "$rc" != 0 ] && log_has "$WORK/sd.err" "BE400317"; then
    echo "sdns skipped: no zlib"
else
    [ "$rc" = 0 ] || fail "encode sdns: exit $rc"
    run_expect_rc 0 "decode sdns" "$BIN" -f bim -s "$WORK/sd" --profile sdns -i "$WORK/sd.bim" -o "$WORK/sd.out.xml"
    assert_contains "$WORK/sd.out.xml" "<Name>Two</Name>" "decode sdns"
fi

# no schema for namespace
sed 's|<TVAMain xmlns="urn:tva:metadata:2004"|<tva:TVAMain xmlns:tva="urn:tva:metadata:2099"|; s|</TVAMain>|</tva:TVAMain>|' "$WORK/in.xml" > "$WORK/ns.xml"
run_expect_rc 1 "unknown namespace" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/ns.xml" -o "$WORK/ns.bim" 2>"$WORK/ns.err"
log_has "$WORK/ns.err" "BE000754" || fail "unknown namespace: wrong error"
log_has "$WORK/ns.err" "urn:tva:metadata:2099" || fail "unknown namespace: not reported"

# extension schema: derived type of a second namespace, decode with and without it
mkdir -p "$WORK/ext/main" "$WORK/ext/more"
sed 's|<xs:element name="Day" type="xs:date" minOccurs="0"/>|&<xs:element name="Related" type="ProgramInformationType" minOccurs="0"/>|' "$WORK/sch/tva.xsd" > "$WORK/ext/main/tva.xsd"
cat > "$WORK/ext/more/ext.xsd" <<'XSD'
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns:t="urn:tva:metadata:2004" xmlns="urn:test:ext" targetNamespace="urn:test:ext" elementFormDefault="qualified">
<xs:import namespace="urn:tva:metadata:2004" schemaLocation="../main/tva.xsd"/>
<xs:complexType name="ExtInfoType"><xs:complexContent><xs:extension base="t:ProgramInformationType"><xs:sequence><xs:element name="Note" type="xs:string"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>
</xs:schema>
XSD
cat > "$WORK/ext/in.xml" <<'XML'
<TVAMain xmlns="urn:tva:metadata:2004" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:x="urn:test:ext"><ProgramDescription><ProgramInformationTable><ProgramInformation programId="crid://a"><Title>Alpha</Title><Related programId="crid://b" xsi:type="x:ExtInfoType"><Title>Beta</Title><x:Note>hello</x:Note></Related></ProgramInformation></ProgramInformationTable></ProgramDescription></TVAMain>
XML
for p in dvb-tva tva generic; do
    "$BIN" -f xml -s "$WORK/ext/main" -s "$WORK/ext/more" --profile "$p" -i "$WORK/ext/in.xml" -o "$WORK/ext/o.$p.bim" 2>"$WORK/enc.err"
    rc=$?
    if [ "$rc" != 0 ] && log_has "$WORK/enc.err" "BE400317"; then
        continue
    fi
    [ "$rc" = 0 ] || fail "extension encode $p: exit $rc"
    grep -q "urn:test:ext" "$WORK/ext/o.$p.bim" || fail "extension encode $p: schema not listed"
    run_expect_rc 0 "extension decode $p" "$BIN" -f bim -s "$WORK/ext/main" -s "$WORK/ext/more" --profile "$p" -i "$WORK/ext/o.$p.bim" -o "$WORK/ext/o.$p.xml"
    assert_contains "$WORK/ext/o.$p.xml" "ExtInfoType" "extension decode $p type"
    assert_contains "$WORK/ext/o.$p.xml" ">hello<" "extension decode $p content"
    run_expect_rc 0 "extension decode $p without schema" "$BIN" -f bim -s "$WORK/ext/main/tva.xsd" --profile "$p" -i "$WORK/ext/o.$p.bim" -o "$WORK/ext/p.$p.xml" 2>"$WORK/ext/p.err"
    log_has "$WORK/ext/p.err" "BW000770" || fail "extension decode $p without schema: no warning"
    assert_contains "$WORK/ext/p.$p.xml" "Beta" "extension decode $p without schema keeps the base content"
    if grep -q "hello" "$WORK/ext/p.$p.xml"; then fail "extension decode $p without schema: chunk not skipped"; fi
done

# text beyond 4 KiB survives every profile
long=$(head -c 6000 /dev/zero | tr '\0' 'x')
sed "s|<Title>Beta</Title>|<Title>$long</Title>|" "$WORK/in.xml" > "$WORK/long.xml"
for p in dvb-tva tva generic; do
    "$BIN" -f xml -s "$WORK/sch" --profile "$p" -i "$WORK/long.xml" -o "$WORK/long.$p.bim" 2>"$WORK/enc.err"
    rc=$?
    if [ "$rc" != 0 ] && log_has "$WORK/enc.err" "BE400317"; then
        continue
    fi
    [ "$rc" = 0 ] || fail "long text encode $p: exit $rc"
    run_expect_rc 0 "long text decode $p" "$BIN" -f bim -s "$WORK/sch" --profile "$p" -i "$WORK/long.$p.bim" -o "$WORK/long.$p.xml"
    assert_contains "$WORK/long.$p.xml" "<Title>$long</Title>" "long text decode $p"
done

# input in a declared non-UTF-8 encoding
printf '<?xml version="1.0" encoding="ISO-8859-1"?>\n<TVAMain xmlns="urn:tva:metadata:2004"><ProgramDescription><ProgramInformationTable><ProgramInformation programId="crid://a"><Title>Gr\374n</Title></ProgramInformation></ProgramInformationTable></ProgramDescription></TVAMain>\n' > "$WORK/latin1.xml"
run_expect_rc 0 "latin-1 encode" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/latin1.xml" -o "$WORK/latin1.bim"
run_expect_rc 0 "latin-1 decode" "$BIN" -f bim -s "$WORK/sch" -i "$WORK/latin1.bim" -o "$WORK/latin1.out.xml"
assert_contains "$WORK/latin1.out.xml" "<Title>Gr$(printf '\303\274')n</Title>" "latin-1 converted to UTF-8"

# attribute the schema does not declare
sed 's|<ProgramInformation programId="crid://example.com/b">|<ProgramInformation programId="crid://example.com/b" rating="5">|' "$WORK/in.xml" > "$WORK/attr.xml"
run_expect_rc 1 "undeclared attribute" "$BIN" -f xml -s "$WORK/sch" -i "$WORK/attr.xml" -o "$WORK/attr.bim" 2>"$WORK/attr.err"
log_has "$WORK/attr.err" "attribute rating of ProgramInformation not declared" || fail "undeclared attribute: not named"

# XSDs: DVBIPITOOLS_XSD_DIR holds one extracted archive per directory (see README, required XSDs)
tva_doc() {
    cat <<XML
<?xml version="1.0" encoding="UTF-8"?>
<TVAMain xmlns="$1" xml:lang="en" publisher="example.com/Guide"><ProgramDescription><ProgramInformationTable><ProgramInformation programId="crid://example.com/a"><BasicDescription><Title>Alpha</Title><Synopsis length="short">First programme</Synopsis></BasicDescription></ProgramInformation></ProgramInformationTable><ProgramLocationTable><Schedule serviceIDRef="ch1" start="2030-01-01T12:00:00Z" end="2030-01-01T13:00:00Z"><ScheduleEvent><Program crid="crid://example.com/a"/><PublishedStartTime>2030-01-01T12:00:00Z</PublishedStartTime><PublishedDuration>PT1H</PublishedDuration></ScheduleEvent></Schedule></ProgramLocationTable><ServiceInformationTable><ServiceInformation serviceId="ch1"><Name>Channel One</Name><Owner>Example</Owner><ServiceURL>dvb://1.2.3</ServiceURL></ServiceInformation></ServiceInformationTable></ProgramDescription></TVAMain>
XML
}

sdns_doc() {
    cat <<XML
<?xml version="1.0" encoding="UTF-8"?>
<ServiceDiscovery xmlns="$1"><BroadcastDiscovery DomainName="example.com" Version="1"><ServiceList><SingleService><ServiceLocation><IPMulticastAddress Address="239.1.1.1" Port="5000"/></ServiceLocation><TextualIdentifier ServiceName="One"/><DVBTriplet OrigNetId="2" TSId="1" ServiceId="101"/></SingleService></ServiceList></BroadcastDiscovery></ServiceDiscovery>
XML
}

real_round_trip() {
    dir=$1 ns=$2 profile=$3 kind=$4
    "${kind}_doc" "$ns" > "$WORK/real.xml"
    "$BIN" -f xml -s "$dir" --profile "$profile" -i "$WORK/real.xml" -o "$WORK/real.bim" 2>"$WORK/real.err"
    rc=$?
    if [ "$rc" != 0 ] && log_has "$WORK/real.err" "BE400317"; then
        return 0
    fi
    [ "$rc" = 0 ] || fail "$ns in $dir: encode exit $rc"
    run_expect_rc 0 "$ns in $dir: decode" "$BIN" -f bim -s "$dir" --profile "$profile" -i "$WORK/real.bim" -o "$WORK/real.out.xml"
    if command -v xmllint >/dev/null 2>&1; then
        [ "$(xmllint --noblanks --c14n "$WORK/real.xml")" = "$(xmllint --noblanks --c14n "$WORK/real.out.xml")" ] || fail "$ns in $dir: round trip differs"
    else
        "$BIN" -f xml -s "$dir" --profile "$profile" -i "$WORK/real.out.xml" -o "$WORK/real2.bim" 2>/dev/null
        cmp -s "$WORK/real.bim" "$WORK/real2.bim" || fail "$ns in $dir: re-encode differs"
    fi
}

if [ -n "$DVBIPITOOLS_XSD_DIR" ]; then
    for d in "$DVBIPITOOLS_XSD_DIR"/*/; do
        for ns in $(grep -rhoE 'targetNamespace="urn:tva:metadata:[0-9]+"' "$d" --include='*.xsd' | sed 's/.*="//;s/"//' | sort -u); do
            real_round_trip "$d" "$ns" dvb-tva tva
        done
        for ns in $(grep -rhoE 'targetNamespace="urn:dvb:(ipi:sdns:[0-9]+|metadata:iptv:sdns:[0-9-]+)"' "$d" --include='*.xsd' | sed 's/.*="//;s/"//' | sort -u); do
            real_round_trip "$d" "$ns" sdns sdns
        done
    done
fi

# MPEG-7 Systems conformance streams: DVBIPITOOLS_MPEG7_CONFORMANCE is the BiM directory of the suite (singleAU, singleAUDecoded),
# DVBIPITOOLS_MPEG7_XSD the MPEG-7 2001 schema. Same element names as the reference decoder's XML
element_names() {
    grep -o '<[A-Za-z][A-Za-z0-9_.:-]*' "$1" | sed 's/^<//; s/^[A-Za-z0-9_.-]*://'
}

if [ -n "$DVBIPITOOLS_MPEG7_CONFORMANCE" ] && [ -n "$DVBIPITOOLS_MPEG7_XSD" ]; then
    di="$DVBIPITOOLS_MPEG7_CONFORMANCE/singleAU/decoderInit.bin"
    for f in "$DVBIPITOOLS_MPEG7_CONFORMANCE"/singleAU/autop*.bin; do
        n=$(basename "$f" .bin)
        { head -c $(($(wc -c < "$di") - 1)) "$di"; cat "$f"; } > "$WORK/conf.bim"
        run_expect_rc 0 "conformance $n" "$BIN" -f bim -s "$DVBIPITOOLS_MPEG7_XSD" --profile generic --syntax 15938-1 --compat conformance -i "$WORK/conf.bim" -o "$WORK/conf.xml"
        element_names "$WORK/conf.xml" > "$WORK/conf.got"
        element_names "$DVBIPITOOLS_MPEG7_CONFORMANCE/singleAUDecoded/$n.bin.xml" > "$WORK/conf.want"
        cmp -s "$WORK/conf.got" "$WORK/conf.want" || fail "conformance $n: elements differ from the reference decoder"
    done
fi

echo "OK"

#EOF
