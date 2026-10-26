/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"

#define WRAP(body) "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\">" body "</xs:schema>"

static int try_doc(const char *text, char *code) {
  xsd_set_t *s = xsd_set_new();
  xml_doc_t *doc = NULL;
  int rc;
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  rc = xsd_set_add_doc(s, doc, "p.xsd");
  if (!rc) rc = xsd_set_resolve(s);
  if (code) {
    const char *c = rc ? xsd_set_error_code(s) : NULL;
    snprintf(code, 16, "%s", c ? c : "");
  }
  xsd_set_free(s);
  return rc;
}

static void rejects(const char *text, const char *want) {
  char code[16];
  ck_assert_int_ne(try_doc(text, code), 0);
  ck_assert_msg(!strcmp(code, want), "wanted %s got %s for %s", want, code, text);
}

START_TEST(xsd_parse_rejects_malformed_declarations) {
  rejects(WRAP("<xs:element name=\"e\" type=\":bad\"/>"), "LS400251");
  rejects(WRAP("<xs:element name=\"e\" type=\"zz:T\"/>"), "LS400252");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"x\"/></xs:sequence></xs:complexType>"), "LS400253");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"-1\"/></xs:sequence></xs:complexType>"), "LS400253");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"18446744073709551615\"/></xs:sequence></xs:complexType>"), "LS400253");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" maxOccurs=\"x\"/></xs:sequence></xs:complexType>"), "LS400254");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" maxOccurs=\"-2\"/></xs:sequence></xs:complexType>"), "LS400254");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" maxOccurs=\"99999999999999999999999\"/></xs:sequence></xs:complexType>"), "LS400254");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"3\" maxOccurs=\"2\"/></xs:sequence></xs:complexType>"), "LS400255");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:any processContents=\"weird\"/></xs:sequence></xs:complexType>"), "LS400256");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:attribute name=\"a\" type=\"xs:string\" use=\"weird\"/></xs:complexType>"), "LS400257");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:length/></xs:restriction></xs:simpleType>"), "LS400258");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:restriction/></xs:simpleType>"), "LS400259");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:list/></xs:simpleType>"), "LS400260");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:union/></xs:simpleType>"), "LS400261");
  rejects(WRAP("<xs:simpleType name=\"S\"/>"), "LS400262");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:attribute type=\"xs:string\"/></xs:complexType>"), "LS400263");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element type=\"xs:string\"/></xs:sequence></xs:complexType>"), "LS400264");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:attributeGroup/></xs:complexType>"), "LS400265");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:group/></xs:complexType>"), "LS400266");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:complexContent/></xs:complexType>"), "LS400267");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:simpleContent/></xs:complexType>"), "LS400267");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:complexContent><xs:extension/></xs:complexContent></xs:complexType>"), "LS400268");
  rejects(WRAP("<xs:simpleType><xs:restriction base=\"xs:string\"/></xs:simpleType>"), "LS400269");
  rejects(WRAP("<xs:complexType/>"), "LS400269");
  rejects(WRAP("<xs:element name=\"e\" ref=\"t:x\"/>"), "LS400270");
  rejects(WRAP("<xs:element name=\"e\" type=\"xs:string\" minOccurs=\"0\"/>"), "LS400270");
  rejects(WRAP("<xs:group><xs:sequence/></xs:group>"), "LS400271");
  rejects(WRAP("<xs:group name=\"G\"/>"), "LS400272");
  rejects(WRAP("<xs:group name=\"G\"><xs:group ref=\"t:H\"/></xs:group>"), "LS400272");
  rejects(WRAP("<xs:attributeGroup/>"), "LS400273");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType><xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"), "LS400274");
  rejects(WRAP("<xs:element name=\"e\" type=\"xs:string\"/><xs:element name=\"e\" type=\"xs:string\"/>"), "LS400274");
  rejects(WRAP("<xs:attribute name=\"a\" type=\"xs:string\"/><xs:attribute name=\"a\" type=\"xs:string\"/>"), "LS400274");
  rejects(WRAP("<xs:group name=\"G\"><xs:sequence/></xs:group><xs:group name=\"G\"><xs:sequence/></xs:group>"), "LS400274");
  rejects(WRAP("<xs:attributeGroup name=\"A\"/><xs:attributeGroup name=\"A\"/>"), "LS400274");
}
END_TEST

static const char rich[] = WRAP(
  "<xs:annotation><xs:documentation>d</xs:documentation></xs:annotation>"
  "<xs:import namespace=\"urn:other\"/><xs:include schemaLocation=\"inc.xsd\"/><xs:redefine schemaLocation=\"red.xsd\"/>"
  "<xs:simpleType name=\" S \"><xs:restriction base=\" xs:string \"><xs:length value=\" 3 \"/><xs:minLength value=\"1\"/><xs:maxLength value=\"9\"/>"
  "<xs:pattern value=\" a+ \"/><xs:enumeration value=\" x \"/><xs:whiteSpace value=\"collapse\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"N\"><xs:restriction base=\"xs:decimal\"><xs:maxInclusive value=\"9\"/><xs:maxExclusive value=\"10\"/><xs:minInclusive value=\"1\"/>"
  "<xs:minExclusive value=\"0\"/><xs:totalDigits value=\"3\"/><xs:fractionDigits value=\"1\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Inl\"><xs:restriction><xs:simpleType><xs:restriction base=\"xs:int\"/></xs:simpleType><xs:minInclusive value=\"1\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"L\"><xs:list itemType=\"t:N\"/></xs:simpleType>"
  "<xs:simpleType name=\"LI\"><xs:list><xs:simpleType><xs:restriction base=\"xs:int\"/></xs:simpleType></xs:list></xs:simpleType>"
  "<xs:simpleType name=\"U\"><xs:union memberTypes=\" t:S   t:N \"><xs:simpleType><xs:restriction base=\"xs:boolean\"/></xs:simpleType></xs:union></xs:simpleType>"
  "<xs:simpleType name=\"UI\"><xs:union><xs:simpleType><xs:restriction base=\"xs:boolean\"/></xs:simpleType></xs:union></xs:simpleType>"
  "<xs:attribute name=\"ga\" type=\"xs:string\" default=\"d\"/>"
  "<xs:attribute name=\"gf\" type=\"xs:string\" fixed=\"f\"/>"
  "<xs:attribute name=\"gi\"><xs:simpleType><xs:restriction base=\"xs:int\"/></xs:simpleType></xs:attribute>"
  "<xs:attributeGroup name=\"AG\"><xs:attribute ref=\"t:ga\" default=\"e\"/><xs:attribute ref=\"t:gf\" fixed=\"f\" use=\"required\"/>"
  "<xs:attribute name=\"p\" type=\"xs:string\" use=\"prohibited\"/><xs:attribute name=\"q\" type=\"xs:string\" form=\"qualified\" use=\"optional\"/>"
  "<xs:attribute name=\"r\" type=\"xs:string\" form=\"unqualified\"/><xs:anyAttribute namespace=\"##local urn:x ##targetNamespace\" processContents=\"skip\"/></xs:attributeGroup>"
  "<xs:group name=\"G\"><xs:choice><xs:element name=\"ge\" type=\"xs:string\"/><xs:sequence><xs:element name=\"gs\" type=\"xs:string\"/></xs:sequence></xs:choice></xs:group>"
  "<xs:complexType name=\"B\" abstract=\"true\" mixed=\"1\"><xs:sequence><xs:element name=\" a \" type=\"xs:string\" minOccurs=\" 0 \" maxOccurs=\" unbounded \"/>"
  "<xs:any namespace=\" ##other \" processContents=\"lax\" minOccurs=\"0\"/><xs:any namespace=\"##any\"/></xs:sequence>"
  "<xs:attributeGroup ref=\"t:AG\"/><xs:anyAttribute namespace=\"##other\"/></xs:complexType>"
  "<xs:complexType name=\"E\"><xs:complexContent mixed=\"true\"><xs:extension base=\"t:B\"><xs:group ref=\"t:G\"/></xs:extension></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"R\"><xs:complexContent mixed=\"false\"><xs:restriction base=\"t:B\"><xs:all><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"0\"/></xs:all></xs:restriction></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"SCe\"><xs:simpleContent><xs:extension base=\"xs:string\"><xs:attribute name=\"u\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:complexType name=\"SCr\"><xs:simpleContent><xs:restriction base=\"t:SCe\"><xs:simpleType><xs:restriction base=\"xs:string\"/></xs:simpleType><xs:maxLength value=\"5\"/></xs:restriction></xs:simpleContent></xs:complexType>"
  "<xs:element name=\"Head\" type=\"t:B\" abstract=\"true\" nillable=\"true\" default=\"x\" fixed=\"x\"/>"
  "<xs:element name=\"Mem\" substitutionGroup=\"t:Head\" type=\"t:E\"/>"
  "<xs:element name=\"InlS\"><xs:simpleType><xs:restriction base=\"xs:int\"/></xs:simpleType></xs:element>"
  "<xs:element name=\"InlC\"><xs:complexType><xs:sequence><xs:element name=\"x\" type=\"xs:string\" form=\"unqualified\"/><xs:element ref=\"t:Head\" minOccurs=\"0\"/></xs:sequence></xs:complexType></xs:element>");

START_TEST(xsd_parse_accepts_every_declaration_form) {
  char code[16];
  ck_assert_int_eq(try_doc(rich, code), 0);
  ck_assert_int_eq(try_doc(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element ref=\"t:e\" minOccurs=\"0\"/></xs:sequence></xs:complexType><xs:element name=\"e\" type=\"xs:string\"/>"), code), 0);
}
END_TEST

START_TEST(xsd_parse_allocation_failures) {
  for (long n = 0;; n++) {
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *doc = NULL;
    int hit;
    ck_assert_int_eq(xml_doc_parse(rich, strlen(rich), &doc, NULL), 0);
    alloc_fail_at(n);
    if (!xsd_set_add_doc(s, doc, "p.xsd")) (void)xsd_set_resolve(s);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

#define BASE_DOC \
  "<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType>" \
  "<xs:complexType name=\"C\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\"/></xs:sequence></xs:complexType>" \
  "<xs:group name=\"G\"><xs:sequence><xs:element name=\"g\" type=\"xs:string\"/></xs:sequence></xs:group>" \
  "<xs:attributeGroup name=\"AG\"><xs:attribute name=\"x\" type=\"xs:string\"/></xs:attributeGroup>"

static const char *redef_try(const char *attrs, const char *base_doc, const char *body, int *rc_out) {
  static char code[16];
  xsd_set_t *s = xsd_set_new();
  xml_doc_t *d1 = NULL;
  xml_doc_t *d2 = NULL;
  const xsd_ref_t *refs = NULL;
  char text[2048];
  int rc = 0;
  int n;

  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(xml_doc_parse(base_doc, strlen(base_doc), &d1, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(s, d1, "base.xsd"), 0);
  snprintf(text, sizeof text, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" %s><xs:redefine schemaLocation=\"base.xsd\">%s</xs:redefine></xs:schema>", attrs, body);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &d2, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(s, d2, "red.xsd"), 0);
  n = xsd_set_doc_refs(s, &refs);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(refs[0].kind, 'r');
  rc = xsd_set_redefine(s, &refs[0], "red.xsd");
  snprintf(code, sizeof code, "%s", rc ? xsd_set_error_code(s) : "");
  xsd_set_free(s);
  *rc_out = rc;
  return code;
}

static const char *const base_full =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\" attributeFormDefault=\"qualified\">" BASE_DOC "</xs:schema>";

START_TEST(xsd_redefine_replaces_and_rejects) {
  int rc;
  const char *code;

  code = redef_try("elementFormDefault=\"qualified\" attributeFormDefault=\"qualified\"", base_full,
                   "<xs:annotation/><xs:element name=\"e\" type=\"xs:string\"/>"
                   "<xs:simpleType name=\"S\"><xs:restriction base=\"t:S\"><xs:maxLength value=\"4\"/></xs:restriction></xs:simpleType>"
                   "<xs:complexType name=\"C\"><xs:complexContent><xs:extension base=\"t:C\"><xs:sequence><xs:element name=\"b\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
                   "<xs:group name=\"G\"><xs:sequence><xs:group ref=\"t:G\"/><xs:element name=\"h\" type=\"xs:string\"/></xs:sequence></xs:group>"
                   "<xs:attributeGroup name=\"AG\"><xs:attributeGroup ref=\"t:AG\"/><xs:attribute name=\"y\" type=\"xs:string\"/></xs:attributeGroup>",
                   &rc);
  ck_assert_msg(rc == 0, "redefine rc %d %s", rc, code);

  code = redef_try("", base_full, "<xs:simpleType><xs:restriction base=\"xs:string\"/></xs:simpleType>", &rc);
  ck_assert_str_eq(code, "LS400277");
  code = redef_try("", base_full, "<xs:simpleType name=\"Nope\"><xs:restriction base=\"t:Nope\"/></xs:simpleType>", &rc);
  ck_assert_str_eq(code, "LS400278");
  code = redef_try("", base_full, "<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType>", &rc);
  ck_assert_str_eq(code, "LS400279");
  code = redef_try("", base_full, "<xs:complexType name=\"C\"><xs:sequence/></xs:complexType>", &rc);
  ck_assert_str_eq(code, "LS400279");
  code = redef_try("", base_full, "<xs:group name=\"Nope\"><xs:sequence/></xs:group>", &rc);
  ck_assert_str_eq(code, "LS400280");
  code = redef_try("", base_full, "<xs:attributeGroup name=\"Nope\"/>", &rc);
  ck_assert_str_eq(code, "LS400281");
  code = redef_try("", base_full, "<xs:group/>", &rc);
  ck_assert_int_ne(rc, 0);
  code = redef_try("", base_full, "<xs:attributeGroup/>", &rc);
  ck_assert_int_ne(rc, 0);
  code = redef_try("", base_full, "<xs:complexType name=\"C\"><xs:complexContent/></xs:complexType>", &rc);
  ck_assert_int_ne(rc, 0);
  (void)code;
}
END_TEST

START_TEST(xsd_include_doc_checks_root_and_namespace) {
  static const char *const inc = "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:simpleType name=\"I\"><xs:restriction base=\"xs:string\"/></xs:simpleType></xs:schema>";
  static const char *const own = "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:other\"/>";
  static const char *const notschema = "<a/>";
  static const char *const texts[] = {inc, own, notschema};
  static const char *const want[] = {"", "LS400275", "LS400250"};

  for (size_t i = 0; i < 3; i++) {
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *doc = NULL;
    int rc;
    ck_assert_ptr_nonnull(s);
    ck_assert_int_eq(xml_doc_parse(texts[i], strlen(texts[i]), &doc, NULL), 0);
    rc = xsd_set_include_doc(s, doc, "inc.xsd", "urn:t");
    ck_assert_int_eq(rc != 0, i != 0);
    if (rc) ck_assert_str_eq(xsd_set_error_code(s), want[i]);
    xsd_set_free(s);
  }
  {
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *doc = NULL;
    ck_assert_int_eq(xml_doc_parse(inc, strlen(inc), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_include_doc(s, doc, "inc.xsd", NULL), 0);
    xsd_set_free(s);
    s = xsd_set_new();
    ck_assert_int_eq(xml_doc_parse(own, strlen(own), &doc, NULL), 0);
    ck_assert_int_ne(xsd_set_include_doc(s, doc, "own.xsd", NULL), 0);
    xsd_set_free(s);
  }
}
END_TEST

START_TEST(xsd_redefine_allocation_failures) {
  for (long n = 0;; n++) {
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *d1 = NULL;
    xml_doc_t *d2 = NULL;
    const xsd_ref_t *refs = NULL;
    static const char *const red =
        "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\"><xs:redefine schemaLocation=\"base.xsd\">"
        "<xs:simpleType name=\"S\"><xs:restriction base=\"t:S\"/></xs:simpleType>"
        "<xs:group name=\"G\"><xs:sequence><xs:group ref=\"t:G\"/></xs:sequence></xs:group>"
        "<xs:attributeGroup name=\"AG\"><xs:attributeGroup ref=\"t:AG\"/></xs:attributeGroup>"
        "</xs:redefine></xs:schema>";
    int hit;
    int cnt;
    ck_assert_int_eq(xml_doc_parse(base_full, strlen(base_full), &d1, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, d1, "base.xsd"), 0);
    ck_assert_int_eq(xml_doc_parse(red, strlen(red), &d2, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, d2, "red.xsd"), 0);
    cnt = xsd_set_doc_refs(s, &refs);
    ck_assert_int_eq(cnt, 1);
    alloc_fail_at(n);
    (void)xsd_set_redefine(s, &refs[0], "red.xsd");
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

static char *wide_doc(int n) {
  size_t cap = (size_t)n * 420 + 4096;
  char *out = calloc(cap, 1);
  ck_assert_ptr_nonnull(out);
  strcat(out, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\">");
  for (int i = 0; i < n; i++) {
    char one[640];
    snprintf(one, sizeof one,
             "<xs:attribute name=\"a%d\" type=\"xs:string\"/><xs:simpleType name=\"S%d\"><xs:restriction base=\"xs:string\"><xs:maxLength value=\"%d\"/></xs:restriction></xs:simpleType>"
             "<xs:complexType name=\"C%d\"><xs:sequence><xs:element name=\"k\" type=\"t:S%d\"/></xs:sequence></xs:complexType>"
             "<xs:element name=\"E%d\" type=\"t:C%d\"/><xs:group name=\"G%d\"><xs:sequence/></xs:group><xs:attributeGroup name=\"AG%d\"/>",
             i, i, i + 1, i, i, i, i, i, i);
    strcat(out, one);
  }
  strcat(out, "<xs:simpleType name=\"Big\"><xs:restriction base=\"xs:string\">");
  for (int i = 0; i < 3000; i++) {
    char one[64];
    snprintf(one, sizeof one, "<xs:enumeration value=\"value-number-%d\"/>", i);
    if (strlen(out) + strlen(one) + 64 < cap) strcat(out, one);
  }
  strcat(out, "</xs:restriction></xs:simpleType></xs:schema>");
  return out;
}

START_TEST(xsd_model_grows_its_tables_and_survives_allocation_failures) {
  char *text = wide_doc(150);
  xsd_set_t *s;
  xml_doc_t *doc = NULL;
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  s = xsd_set_new();
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(xsd_set_add_doc(s, doc, "w.xsd"), 0);
  ck_assert_int_eq(xsd_set_resolve(s), 0);
  xsd_set_free(s);
  for (long n = 0;; n += n < 300 ? 1 : 1 + n / 50) {
    int hit;
    doc = NULL;
    ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
    alloc_fail_at(n);
    s = xsd_set_new();
    if (s) {
      if (!xsd_set_add_doc(s, doc, "w.xsd")) (void)xsd_set_resolve(s);
    } else {
      xml_doc_free(doc);
    }
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
  free(text);
}
END_TEST

START_TEST(xsd_resolve_reports_each_unresolved_reference) {
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:restriction base=\"t:Nope\"/></xs:simpleType>"), "LS400282");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:list itemType=\"t:Nope\"/></xs:simpleType>"), "LS400282");
  rejects(WRAP("<xs:simpleType name=\"S\"><xs:union memberTypes=\"xs:string t:Nope\"/></xs:simpleType>"), "LS400282");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:attribute ref=\"t:nope\"/></xs:complexType>"), "LS400282");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:attributeGroup ref=\"t:nope\"/></xs:complexType>"), "LS400282");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:element ref=\"t:nope\"/></xs:sequence></xs:complexType>"), "LS400282");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:sequence><xs:group ref=\"t:nope\"/></xs:sequence></xs:complexType>"), "LS400282");
  rejects(WRAP("<xs:attribute name=\"a\" type=\"t:Nope\"/>"), "LS400282");
  rejects(WRAP("<xs:element name=\"e\" substitutionGroup=\"t:nope\" type=\"xs:string\"/>"), "LS400282");
  rejects(WRAP("<xs:element name=\"e\" type=\"t:Nope\"/>"), "LS400282");
  rejects(WRAP("<xs:complexType name=\"T\"><xs:complexContent><xs:extension base=\"t:Nope\"/></xs:complexContent></xs:complexType>"), "LS400282");
}
END_TEST

START_TEST(xsd_resolve_fills_defaults_and_inherits_substitution_types) {
  char code[16];
  ck_assert_int_eq(try_doc(WRAP("<xs:attribute name=\"a\"/><xs:element name=\"plain\"/>"
                                "<xs:element name=\"head\" type=\"xs:int\"/>"
                                "<xs:element name=\"m1\" substitutionGroup=\"t:head\"/>"
                                "<xs:element name=\"m2\" substitutionGroup=\"t:m1\"/>"
                                "<xs:element name=\"m0\" substitutionGroup=\"t:m2\"/>"
                                "<xs:simpleType name=\"S\"><xs:restriction/></xs:simpleType>"),
                           code),
                   -1);
  ck_assert_int_eq(try_doc(WRAP("<xs:attribute name=\"a\"/><xs:element name=\"plain\"/>"
                                "<xs:element name=\"head\" type=\"xs:int\"/>"
                                "<xs:element name=\"m1\" substitutionGroup=\"t:head\"/>"
                                "<xs:element name=\"m2\" substitutionGroup=\"t:m1\"/>"
                                "<xs:element name=\"m0\" substitutionGroup=\"t:m2\"/>"),
                           code),
                   0);
}
END_TEST

static Suite *parse_suite(void) {
  Suite *s = suite_create("bim_xsd_parse");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xsd_parse_rejects_malformed_declarations);
  tcase_add_test(tc, xsd_parse_accepts_every_declaration_form);
  tcase_add_test(tc, xsd_parse_allocation_failures);
  tcase_add_test(tc, xsd_redefine_replaces_and_rejects);
  tcase_add_test(tc, xsd_include_doc_checks_root_and_namespace);
  tcase_add_test(tc, xsd_redefine_allocation_failures);
  tcase_add_test(tc, xsd_resolve_reports_each_unresolved_reference);
  tcase_add_test(tc, xsd_resolve_fills_defaults_and_inherits_substitution_types);
  tcase_add_test(tc, xsd_model_grows_its_tables_and_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(parse_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
