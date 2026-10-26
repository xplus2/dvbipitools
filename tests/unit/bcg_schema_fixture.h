/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_BCG_SCHEMA_FIXTURE_H
#define DVBIPITOOLS_TESTS_UNIT_BCG_SCHEMA_FIXTURE_H

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* TV-Anytime subset of fragments dipibcg builds, in a temporary directory with the xml namespace schema it imports */

#define BCG_FIXTURE_NS "urn:tva:metadata:2004"

static const char bcg_fixture_xml_xsd[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"http://www.w3.org/XML/1998/namespace\">"
  "<xs:attribute name=\"lang\" type=\"xs:language\"/></xs:schema>";

static const char bcg_fixture_tva_xsd[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" BCG_FIXTURE_NS "\" targetNamespace=\"" BCG_FIXTURE_NS "\" elementFormDefault=\"qualified\">"
  "<xs:import namespace=\"http://www.w3.org/XML/1998/namespace\" schemaLocation=\"xml.xsd\"/>"
  "<xs:complexType name=\"GenreType\"><xs:sequence><xs:element name=\"Name\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence><xs:attribute name=\"href\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"BasicDescriptionType\"><xs:sequence><xs:element name=\"Title\" type=\"xs:string\" minOccurs=\"0\"/>"
  "<xs:element name=\"Synopsis\" type=\"xs:string\" minOccurs=\"0\"/><xs:element name=\"Genre\" type=\"GenreType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ProgramInformationType\"><xs:sequence><xs:element name=\"BasicDescription\" type=\"BasicDescriptionType\"/></xs:sequence>"
  "<xs:attribute name=\"programId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"ProgramInformationTableType\"><xs:sequence><xs:element name=\"ProgramInformation\" type=\"ProgramInformationType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ProgramRefType\"><xs:attribute name=\"crid\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"InstanceDescriptionType\"><xs:sequence><xs:element name=\"Title\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ScheduleEventType\"><xs:sequence><xs:element name=\"Program\" type=\"ProgramRefType\"/>"
  "<xs:element name=\"InstanceDescription\" type=\"InstanceDescriptionType\" minOccurs=\"0\"/>"
  "<xs:element name=\"PublishedStartTime\" type=\"xs:dateTime\"/><xs:element name=\"PublishedEndTime\" type=\"xs:dateTime\" minOccurs=\"0\"/>"
  "<xs:element name=\"PublishedDuration\" type=\"xs:duration\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ScheduleType\"><xs:sequence><xs:element name=\"ScheduleEvent\" type=\"ScheduleEventType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"serviceIDRef\" type=\"xs:string\" use=\"required\"/><xs:attribute name=\"start\" type=\"xs:dateTime\"/><xs:attribute name=\"end\" type=\"xs:dateTime\"/></xs:complexType>"
  "<xs:complexType name=\"ProgramLocationTableType\"><xs:sequence><xs:element name=\"Schedule\" type=\"ScheduleType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ServiceInformationType\"><xs:sequence><xs:element name=\"Name\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"
  "<xs:element name=\"ServiceURL\" type=\"xs:anyURI\" minOccurs=\"0\"/></xs:sequence><xs:attribute name=\"serviceId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"ServiceInformationTableType\"><xs:sequence><xs:element name=\"ServiceInformation\" type=\"ServiceInformationType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ProgramDescriptionType\"><xs:sequence><xs:element name=\"ProgramInformationTable\" type=\"ProgramInformationTableType\" minOccurs=\"0\"/>"
  "<xs:element name=\"ProgramLocationTable\" type=\"ProgramLocationTableType\" minOccurs=\"0\"/>"
  "<xs:element name=\"ServiceInformationTable\" type=\"ServiceInformationTableType\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"TVAMainType\"><xs:sequence><xs:element name=\"ProgramDescription\" type=\"ProgramDescriptionType\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"publisher\" type=\"xs:string\"/><xs:attribute ref=\"xml:lang\"/></xs:complexType>"
  "<xs:element name=\"TVAMain\" type=\"TVAMainType\"/></xs:schema>";

static inline void bcg_fixture_put(const char *dir, const char *name, const char *body) {
  char path[300];
  FILE *f;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  f = fopen(path, "w");
  if (!f) abort();
  ck_assert_uint_eq(fwrite(body, 1, strlen(body), f), strlen(body));
  fclose(f);
}

/* dir: char[64], set to new directory */
static inline void bcg_fixture_make(char *dir) {
  snprintf(dir, 64, "/tmp/dvbipitools_bcgsch_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(dir));
  bcg_fixture_put(dir, "xml.xsd", bcg_fixture_xml_xsd);
  bcg_fixture_put(dir, "tva.xsd", bcg_fixture_tva_xsd);
}

static inline void bcg_fixture_remove(const char *dir) {
  char path[300];
  snprintf(path, sizeof path, "%s/xml.xsd", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/tva.xsd", dir);
  unlink(path);
  rmdir(dir);
}

#endif
