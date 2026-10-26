/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"

#define WRAP(body) "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\">" body "</xs:schema>"

typedef struct {
  xsd_set_t *s;
  const xsd_compiled_t *c;
} cx_t;

static int compile(const char *doc_text, unsigned compat, const xsd_codec_map_t *codecs, int ncodecs, int zlib, cx_t *x, char *code) {
  xsd_compile_opts_t o = {1, zlib, 0, codecs, ncodecs, compat};
  xml_doc_t *doc = NULL;
  int rc;
  x->s = xsd_set_new();
  ck_assert_ptr_nonnull(x->s);
  ck_assert_int_eq(xml_doc_parse(doc_text, strlen(doc_text), &doc, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(x->s, doc, "s.xsd"), 0);
  ck_assert_int_eq(xsd_set_resolve(x->s), 0);
  rc = xsd_compile(x->s, &o, &x->c);
  if (code) snprintf(code, 16, "%s", rc ? xsd_set_error_code(x->s) : "");
  return rc;
}

static const bim_simple_t *simple_of(const cx_t *x, const char *name) {
  const bim_type_t *t = xsd_compiled_type(x->c, "urn:t", name);
  ck_assert_ptr_nonnull(t);
  return t->simple;
}

static const bim_simple_t *elem_simple(const cx_t *x, const char *name) {
  const bim_elem_t *e = xsd_compiled_elem(x->c, "urn:t", name);
  ck_assert_ptr_nonnull(e);
  return e->type->simple;
}

static void int_facets(const char *a, const char *b, const char *want_code, bim_codec_t want_codec) {
  char doc[1024];
  char code[16];
  cx_t x;
  snprintf(doc, sizeof doc, WRAP("<xs:element name=\"E\"><xs:simpleType><xs:restriction base=\"xs:integer\">%s%s</xs:restriction></xs:simpleType></xs:element>"), a, b);
  if (want_code) {
    ck_assert_msg(compile(doc, 0, NULL, 0, 0, &x, code) != 0, "%s %s compiled", a, b);
    ck_assert_msg(!strcmp(code, want_code), "%s %s: %s", a, b, code);
  } else {
    ck_assert_int_eq(compile(doc, 0, NULL, 0, 0, &x, code), 0);
    ck_assert_int_eq(elem_simple(&x, "E")->codec, want_codec);
  }
  xsd_set_free(x.s);
}

START_TEST(simple_integer_ranges) {
  int_facets("<xs:minInclusive value=\"1\"/>", "<xs:maxInclusive value=\"9\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minInclusive value=\"-5\"/>", "<xs:maxInclusive value=\"9\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minInclusive value=\"-9\"/>", "<xs:maxInclusive value=\"-5\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minExclusive value=\"0\"/>", "<xs:maxExclusive value=\"10\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minExclusive value=\"-1\"/>", "<xs:maxExclusive value=\"1\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minInclusive value=\"+3\"/>", "<xs:maxInclusive value=\"3\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minInclusive value=\"0\"/>", "", NULL, BIM_SVLU5);
  int_facets("", "<xs:maxInclusive value=\"0\"/>", NULL, BIM_SVLU5);
  int_facets("", "", NULL, BIM_SVLU5);
  int_facets("<xs:minInclusive value=\"abc\"/>", "<xs:maxInclusive value=\"1\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"1\"/>", "<xs:maxInclusive value=\"\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"1\"/>", "<xs:maxInclusive value=\"99999999999999999999\"/>", "LS400283", 0);
  int_facets("<xs:minExclusive value=\"18446744073709551615\"/>", "<xs:maxInclusive value=\"1\"/>", "LS400283", 0);
  int_facets("<xs:minExclusive value=\"x\"/>", "<xs:maxInclusive value=\"1\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"1\"/>", "<xs:maxExclusive value=\"-18446744073709551615\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"1\"/>", "<xs:maxExclusive value=\"x\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"9223372036854775808\"/>", "<xs:maxInclusive value=\"9223372036854775809\"/>", "LS400283", 0);
  int_facets("<xs:minInclusive value=\"5\"/>", "<xs:maxInclusive value=\"1\"/>", "LS400284", 0);
  int_facets("<xs:minInclusive value=\"-1\"/>", "<xs:maxInclusive value=\"-5\"/>", "LS400284", 0);
  int_facets("<xs:minInclusive value=\"-5\"/>", "<xs:maxInclusive value=\"18446744073709551615\"/>", "LS400284", 0);
  int_facets("<xs:minInclusive value=\"-9223372036854775808\"/>", "<xs:maxInclusive value=\"9223372036854775807\"/>", NULL, BIM_RANGE);
  int_facets("<xs:minInclusive value=\"-9223372036854775808\"/>", "<xs:maxInclusive value=\"-9223372036854775808\"/>", NULL, BIM_RANGE);
}
END_TEST

static void expect_compile_fails(const char *body, const char *want) {
  char code[16];
  char doc[2048];
  cx_t x;
  snprintf(doc, sizeof doc, WRAP("%s"), body);
  ck_assert_int_ne(compile(doc, 0, NULL, 0, 0, &x, code), 0);
  ck_assert_msg(!strcmp(code, want), "%s: %s", body, code);
  xsd_set_free(x.s);
}

START_TEST(simple_lists_enumerations_and_patterns) {
  cx_t x;
  char code[16];
  expect_compile_fails("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:length value=\"-1\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:L\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:minLength value=\"x\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:L\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:maxLength value=\"x\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:L\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:minLength value=\"5\"/><xs:maxLength value=\"2\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:L\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:length value=\"x\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:S\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:minLength value=\"x\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:S\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:maxLength value=\"-2\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:S\"/>", "LS400285");
  expect_compile_fails("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"(\"/></xs:restriction></xs:simpleType><xs:element name=\"E\" type=\"t:S\"/>", "LS400290");

  ck_assert_int_eq(compile(WRAP("<xs:simpleType name=\"L1\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:length value=\"3\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"L2\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:minLength value=\"1\"/><xs:maxLength value=\"4\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"L3\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:minLength value=\"2\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"L4\"><xs:list itemType=\"xs:int\"/></xs:simpleType>"
                               "<xs:simpleType name=\"S1\"><xs:restriction base=\"xs:string\"><xs:length value=\"3\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"S2\"><xs:restriction base=\"xs:string\"><xs:minLength value=\"1\"/><xs:maxLength value=\"4\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"S3\"><xs:restriction base=\"xs:string\"><xs:minLength value=\"2\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"S4\"><xs:restriction base=\"t:S3\"><xs:maxLength value=\"9\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"P1\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"a+\"/><xs:pattern value=\"[ab]+\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"P2\"><xs:restriction base=\"t:P1\"><xs:pattern value=\"a.*\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"En\"><xs:restriction base=\"xs:string\"><xs:enumeration value=\"b\"/><xs:enumeration value=\"a\"/><xs:enumeration value=\"b\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"En2\"><xs:restriction base=\"t:En\"><xs:enumeration value=\"a\"/></xs:restriction></xs:simpleType>"
                               "<xs:simpleType name=\"U\"><xs:union memberTypes=\"t:En xs:int\"/></xs:simpleType>"
                               "<xs:simpleType name=\"Ids\"><xs:restriction base=\"xs:IDREFS\"><xs:length value=\"2\"/></xs:restriction></xs:simpleType>"
                               "<xs:element name=\"Bad\" type=\"xs:NOTATION\"/>"),
                      0, NULL, 0, 0, &x, code),
                   0);
  ck_assert_int_eq(simple_of(&x, "L1")->codec, BIM_LIST);
  ck_assert_int_eq(simple_of(&x, "L1")->unbounded, 0);
  ck_assert_int_eq(simple_of(&x, "L2")->unbounded, 0);
  ck_assert_int_eq(simple_of(&x, "L3")->unbounded, 1);
  ck_assert_int_eq(simple_of(&x, "L4")->unbounded, 1);
  ck_assert_int_eq(simple_of(&x, "S1")->has_length, 1);
  ck_assert_int_eq(simple_of(&x, "S2")->has_length, 1);
  ck_assert_int_eq(simple_of(&x, "S3")->has_length, 1);
  ck_assert_int_eq(simple_of(&x, "P1")->npatterns, 1);
  ck_assert_int_eq(simple_of(&x, "P2")->npatterns, 2);
  ck_assert_int_eq(simple_of(&x, "En")->nenums, 2);
  ck_assert_int_eq(simple_of(&x, "En2")->codec, BIM_ENUM);
  ck_assert_int_eq(simple_of(&x, "U")->codec, BIM_UNION);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(simple_list_builtins_as_strings_and_zlib) {
  cx_t x;
  char code[16];
  static const char doc[] = WRAP("<xs:element name=\"A\" type=\"xs:IDREFS\"/><xs:element name=\"B\" type=\"xs:string\"/><xs:element name=\"C\" type=\"xs:NMTOKENS\"/>");
  ck_assert_int_eq(compile(doc, BIM_COMPAT_LIST_STRING, NULL, 0, 0, &x, code), 0);
  ck_assert_int_eq(elem_simple(&x, "A")->codec, BIM_UTF8);
  xsd_set_free(x.s);
  ck_assert_int_eq(compile(doc, 0, NULL, 0, 1, &x, code), 0);
  ck_assert_int_eq(elem_simple(&x, "B")->codec, BIM_ZSTRING);
  ck_assert_int_eq(elem_simple(&x, "A")->codec, BIM_LIST);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(simple_codec_map_and_simple_content) {
  static const xsd_codec_map_t maps[] = {{"urn:t", "Special", BIM_HEXBIN, 0}, {"urn:t", "Base", BIM_BASE64, 1}, {NULL, "none", BIM_DOUBLE, 1}};
  cx_t x;
  char code[16];
  static const char doc[] = WRAP("<xs:simpleType name=\"Special\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
                                 "<xs:simpleType name=\"Under\"><xs:restriction base=\"t:Special\"/></xs:simpleType>"
                                 "<xs:simpleType name=\"Base\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
                                 "<xs:simpleType name=\"Below\"><xs:restriction base=\"t:Base\"><xs:length value=\"3\"/></xs:restriction></xs:simpleType>"
                                 "<xs:complexType name=\"CE\"><xs:simpleContent><xs:extension base=\"xs:int\"><xs:attribute name=\"a\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
                                 "<xs:complexType name=\"CR\"><xs:simpleContent><xs:restriction base=\"t:CE\"><xs:minInclusive value=\"1\"/><xs:maxInclusive value=\"5\"/></xs:restriction></xs:simpleContent></xs:complexType>"
                                 "<xs:complexType name=\"CS\"><xs:simpleContent><xs:restriction base=\"t:CE\"><xs:simpleType><xs:restriction base=\"xs:int\"/></xs:simpleType></xs:restriction></xs:simpleContent></xs:complexType>"
                                 "<xs:complexType name=\"CX\"><xs:simpleContent><xs:extension base=\"t:CR\"/></xs:simpleContent></xs:complexType>"
                                 "<xs:element name=\"E1\" type=\"t:CE\"/><xs:element name=\"E2\" type=\"t:CR\"/><xs:element name=\"E3\" type=\"t:CS\"/><xs:element name=\"E4\" type=\"t:CX\"/>");
  ck_assert_int_eq(compile(doc, 0, maps, 3, 0, &x, code), 0);
  ck_assert_int_eq(simple_of(&x, "Special")->codec, BIM_HEXBIN);
  ck_assert_int_eq(simple_of(&x, "Under")->codec, BIM_UTF8);
  ck_assert_int_eq(simple_of(&x, "Base")->codec, BIM_BASE64);
  ck_assert_int_eq(simple_of(&x, "Below")->codec, BIM_BASE64);
  ck_assert_ptr_nonnull(xsd_compiled_type(x.c, "urn:t", "CE")->simple);
  ck_assert_int_eq(xsd_compiled_type(x.c, "urn:t", "CR")->simple->codec, BIM_RANGE);
  ck_assert_ptr_nonnull(xsd_compiled_type(x.c, "urn:t", "CS")->simple);
  ck_assert_ptr_nonnull(xsd_compiled_type(x.c, "urn:t", "CX")->simple);
  xsd_set_free(x.s);
}
END_TEST

static char *chain(int n, int complex) {
  size_t cap = (size_t)n * 260 + 400;
  char *d = calloc(cap, 1);
  size_t at = 0;
  ck_assert_ptr_nonnull(d);
  at += (size_t)snprintf(d + at, cap - at, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\">");
  if (complex) {
    at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C0\"><xs:simpleContent><xs:extension base=\"xs:int\"/></xs:simpleContent></xs:complexType>");
    for (int i = 1; i < n; i++)
      at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C%d\"><xs:simpleContent><xs:restriction base=\"t:C%d\"><xs:minInclusive value=\"%d\"/></xs:restriction></xs:simpleContent></xs:complexType>", i, i - 1, i % 7);
    at += (size_t)snprintf(d + at, cap - at, "<xs:element name=\"E\" type=\"t:C%d\"/>", n - 1);
  } else {
    at += (size_t)snprintf(d + at, cap - at, "<xs:simpleType name=\"S0\"><xs:restriction base=\"xs:string\"/></xs:simpleType>");
    for (int i = 1; i < n; i++) at += (size_t)snprintf(d + at, cap - at, "<xs:simpleType name=\"S%d\"><xs:restriction base=\"t:S%d\"/></xs:simpleType>", i, i - 1);
    at += (size_t)snprintf(d + at, cap - at, "<xs:element name=\"E\" type=\"t:S%d\"/>", n - 1);
  }
  snprintf(d + at, cap - at, "</xs:schema>");
  return d;
}

START_TEST(simple_derivation_depth_limit) {
  for (int complex = 0; complex < 2; complex++) {
    cx_t x;
    char code[16];
    char *d = chain(300, complex);
    (void)compile(d, 0, NULL, 0, 0, &x, code);
    xsd_set_free(x.s);
    free(d);
  }
}
END_TEST

START_TEST(simple_compile_allocation_failures) {
  static const char doc[] = WRAP("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:maxLength value=\"4\"/></xs:restriction></xs:simpleType>"
                                 "<xs:simpleType name=\"P\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"a+\"/><xs:enumeration value=\"a\"/><xs:enumeration value=\"b\"/></xs:restriction></xs:simpleType>"
                                 "<xs:simpleType name=\"U\"><xs:union memberTypes=\"t:P xs:int t:L\"/></xs:simpleType>"
                                 "<xs:simpleType name=\"I\"><xs:restriction base=\"xs:integer\"><xs:minInclusive value=\"1\"/><xs:maxInclusive value=\"9\"/></xs:restriction></xs:simpleType>"
                                 "<xs:complexType name=\"C\"><xs:simpleContent><xs:restriction base=\"t:CB\"><xs:minInclusive value=\"1\"/></xs:restriction></xs:simpleContent></xs:complexType>"
                                 "<xs:complexType name=\"CB\"><xs:simpleContent><xs:extension base=\"xs:int\"/></xs:simpleContent></xs:complexType>"
                                 "<xs:element name=\"E\" type=\"t:U\"/><xs:element name=\"F\" type=\"t:C\"/><xs:element name=\"G\" type=\"t:I\"/>");
  for (long n = 0;; n++) {
    xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *doc_parsed = NULL;
    int hit;
    ck_assert_int_eq(xml_doc_parse(doc, strlen(doc), &doc_parsed, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, doc_parsed, "s.xsd"), 0);
    ck_assert_int_eq(xsd_set_resolve(s), 0);
    alloc_fail_at(n);
    (void)xsd_compile(s, &o, &c);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

static Suite *simple_suite(void) {
  Suite *s = suite_create("bim_xsd_simple");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, simple_integer_ranges);
  tcase_add_test(tc, simple_lists_enumerations_and_patterns);
  tcase_add_test(tc, simple_list_builtins_as_strings_and_zlib);
  tcase_add_test(tc, simple_codec_map_and_simple_content);
  tcase_add_test(tc, simple_derivation_depth_limit);
  tcase_add_test(tc, simple_compile_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(simple_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
