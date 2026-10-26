# Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
# See NOTICE and LICENSE for details and authorship information.

# sourced after common.sh by the dipibim scripts. Schemas and documents
# below $WORK: $WORK/sch (TVA subset), $WORK/sd (SD&S subset), $WORK/in.xml, $WORK/sd.xml

make_fixtures() {
    mkdir -p "$WORK/sch" "$WORK/sd"
    cat > "$WORK/sch/tva.xsd" <<'XSD'
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns="urn:tva:metadata:2004" targetNamespace="urn:tva:metadata:2004" elementFormDefault="qualified">
<xs:complexType name="ProgramInformationType"><xs:sequence><xs:element name="Title" type="xs:string" minOccurs="0"/>
<xs:element name="Created" type="xs:dateTime" minOccurs="0"/><xs:element name="Day" type="xs:date" minOccurs="0"/></xs:sequence>
<xs:attribute name="programId" type="xs:string" use="required"/></xs:complexType>
<xs:complexType name="ProgramInformationTableType"><xs:sequence><xs:element name="ProgramInformation" type="ProgramInformationType" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
<xs:complexType name="ServiceInformationType"><xs:sequence><xs:element name="Name" type="xs:string"/><xs:element name="ServiceURL" type="xs:anyURI" minOccurs="0"/></xs:sequence>
<xs:attribute name="serviceId" type="xs:string" use="required"/></xs:complexType>
<xs:complexType name="ServiceInformationTableType"><xs:sequence><xs:element name="ServiceInformation" type="ServiceInformationType" minOccurs="0" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
<xs:complexType name="ProgramDescriptionType"><xs:sequence><xs:element name="ProgramInformationTable" type="ProgramInformationTableType" minOccurs="0"/>
<xs:element name="ServiceInformationTable" type="ServiceInformationTableType" minOccurs="0"/></xs:sequence></xs:complexType>
<xs:complexType name="TVAMainType"><xs:sequence><xs:element name="ProgramDescription" type="ProgramDescriptionType" minOccurs="0"/></xs:sequence>
<xs:attribute name="publisher" type="xs:string"/></xs:complexType>
<xs:element name="TVAMain" type="TVAMainType"/></xs:schema>
XSD
    cat > "$WORK/sd/sdns.xsd" <<'XSD'
<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema" xmlns="urn:dvb:metadata:iptv:sdns:2012-1" targetNamespace="urn:dvb:metadata:iptv:sdns:2012-1" elementFormDefault="qualified">
<xs:complexType name="ServiceType"><xs:sequence><xs:element name="Name" type="xs:string"/></xs:sequence><xs:attribute name="id" type="xs:string"/></xs:complexType>
<xs:complexType name="ServiceDiscoveryType"><xs:sequence><xs:element name="Service" type="ServiceType" maxOccurs="unbounded"/></xs:sequence></xs:complexType>
<xs:element name="ServiceDiscovery" type="ServiceDiscoveryType"/></xs:schema>
XSD
    cat > "$WORK/in.xml" <<'XML'
<?xml version="1.0" encoding="UTF-8"?>
<TVAMain xmlns="urn:tva:metadata:2004" publisher="tvlistings.tv/TV%20Listings">
<ProgramDescription>
<ProgramInformationTable>
<ProgramInformation programId="crid://example.com/a"><Title>Alpha</Title><Created>2024-05-01T10:00:00Z</Created><Day>2024-05-01</Day></ProgramInformation>
<ProgramInformation programId="crid://example.com/b"><Title>Beta</Title></ProgramInformation>
</ProgramInformationTable>
<ServiceInformationTable>
<ServiceInformation serviceId="channel1"><Name>Channel One</Name><ServiceURL>dvb://1.2.3</ServiceURL></ServiceInformation>
</ServiceInformationTable>
</ProgramDescription>
</TVAMain>
XML
    cat > "$WORK/sd.xml" <<'XML'
<ServiceDiscovery xmlns="urn:dvb:metadata:iptv:sdns:2012-1"><Service id="1"><Name>One</Name></Service><Service id="2"><Name>Two</Name></Service></ServiceDiscovery>
XML
}

# DVB-TVA-init and DecoderInit for urn:tva:metadata:2004
DVB_INIT='\360\177\005\077\001\000\037\001\025urn:tva:metadata:2004\000\000\000'
