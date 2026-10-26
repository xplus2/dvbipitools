/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"

#define XS "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "

typedef struct {
  xsd_set_t *s;
  const xsd_compiled_t *c;
} cx_t;

static xsd_set_t *load(const char *const *docs, int n) {
  xsd_set_t *s = xsd_set_new();
  ck_assert_ptr_nonnull(s);
  for (int i = 0; i < n; i++) {
    xml_doc_t *doc = NULL;
    ck_assert_int_eq(xml_doc_parse(docs[i], strlen(docs[i]), &doc, NULL), 0);
    if (xsd_set_add_doc(s, doc, "w.xsd")) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
  }
  if (xsd_set_resolve(s)) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
  return s;
}

static int compile(const char *const *docs, int n, cx_t *x, const char **code) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  int rc;
  x->s = load(docs, n);
  rc = xsd_compile(x->s, &o, &x->c);
  *code = rc ? xsd_set_error_code(x->s) : NULL;
  return rc;
}

static const bim_wildcard_t *wc_of(const cx_t *x, const char *ns, const char *type) {
  const bim_type_t *t = xsd_compiled_type(x->c, ns, type);
  ck_assert_ptr_nonnull(t);
  return t->anyattr;
}

#define DOC_T(body) "<xs:schema " XS "xmlns:t=\"urn:t\" targetNamespace=\"urn:t\">" body "</xs:schema>"
#define DOC_V(body) "<xs:schema " XS "xmlns:v=\"urn:v\" targetNamespace=\"urn:v\">" body "</xs:schema>"
#define DOC_N(body) "<xs:schema " XS ">" body "</xs:schema>"

static int has_ns(const bim_wildcard_t *w, const char *ns) {
  for (int i = 0; i < w->nns; i++)
    if ((!w->ns[i] && !ns) || (w->ns[i] && ns && !strcmp(w->ns[i], ns))) return 1;
  return 0;
}

static const bim_wildcard_t *meet(cx_t *x, const char *own, const char *group) {
  char doc[1024];
  const char *docs[1];
  const char *code;
  snprintf(doc, sizeof doc,
           DOC_T("<xs:attributeGroup name=\"G\"><xs:anyAttribute namespace=\"%s\"/></xs:attributeGroup>"
                 "<xs:complexType name=\"T\"><xs:attributeGroup ref=\"t:G\"/><xs:anyAttribute namespace=\"%s\"/></xs:complexType>"),
           group, own);
  docs[0] = doc;
  ck_assert_int_eq(compile(docs, 1, x, &code), 0);
  return wc_of(x, "urn:t", "T");
}

static const bim_wildcard_t *join(cx_t *x, const char *own, const char *base) {
  char doc[1024];
  const char *docs[1];
  const char *code;
  snprintf(doc, sizeof doc,
           DOC_T("<xs:complexType name=\"B\"><xs:anyAttribute namespace=\"%s\"/></xs:complexType>"
                 "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"t:B\"><xs:anyAttribute namespace=\"%s\"/></xs:extension></xs:complexContent></xs:complexType>"),
           base, own);
  docs[0] = doc;
  ck_assert_int_eq(compile(docs, 1, x, &code), 0);
  return wc_of(x, "urn:t", "D");
}

START_TEST(attrs_wildcard_intersection) {
  cx_t x;
  const bim_wildcard_t *w;
  w = meet(&x, "urn:u", "urn:u");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(w->nns, 1);
  xsd_set_free(x.s);
  w = meet(&x, "urn:u", "##any");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  xsd_set_free(x.s);
  w = meet(&x, "##any", "urn:u");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(has_ns(w, "urn:u"), 1);
  xsd_set_free(x.s);
  w = meet(&x, "##other", "##other");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  xsd_set_free(x.s);
  w = meet(&x, "##other", "urn:t urn:u ##local");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(w->nns, 1);
  ck_assert_int_eq(has_ns(w, "urn:u"), 1);
  xsd_set_free(x.s);
  w = meet(&x, "urn:t urn:u ##local", "##other");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(w->nns, 1);
  xsd_set_free(x.s);
  w = meet(&x, "urn:u urn:v", "urn:v urn:w");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(w->nns, 1);
  ck_assert_int_eq(has_ns(w, "urn:v"), 1);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(attrs_wildcard_intersection_of_negations) {
  cx_t x;
  const char *code;
  const char *docs[2];
  const bim_wildcard_t *w;
  docs[0] = DOC_N("<xs:attributeGroup name=\"G\"><xs:anyAttribute namespace=\"##other\"/></xs:attributeGroup>");
  docs[1] = DOC_T("<xs:import/><xs:complexType name=\"T\"><xs:attributeGroup ref=\"G\"/><xs:anyAttribute namespace=\"##other\"/></xs:complexType>");
  ck_assert_int_eq(compile(docs, 2, &x, &code), 0);
  w = wc_of(&x, "urn:t", "T");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  ck_assert_ptr_nonnull(w->ns[0]);
  xsd_set_free(x.s);
  docs[0] = DOC_T("<xs:import/><xs:attributeGroup name=\"G\"><xs:anyAttribute namespace=\"##other\"/></xs:attributeGroup>");
  docs[1] = "<xs:schema " XS "xmlns:t=\"urn:t\"><xs:import namespace=\"urn:t\"/><xs:complexType name=\"T\"><xs:attributeGroup ref=\"t:G\"/><xs:anyAttribute namespace=\"##other\"/></xs:complexType></xs:schema>";
  ck_assert_int_eq(compile(docs, 2, &x, &code), 0);
  w = wc_of(&x, NULL, "T");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  ck_assert_ptr_nonnull(w->ns[0]);
  xsd_set_free(x.s);
  docs[0] = DOC_V("<xs:attributeGroup name=\"G\"><xs:anyAttribute namespace=\"##other\"/></xs:attributeGroup>");
  docs[1] = "<xs:schema " XS "xmlns:v=\"urn:v\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\"><xs:import namespace=\"urn:v\"/>"
            "<xs:complexType name=\"T\"><xs:attributeGroup ref=\"v:G\"/><xs:anyAttribute namespace=\"##other\"/></xs:complexType></xs:schema>";
  ck_assert_int_eq(compile(docs, 2, &x, &code), -1);
  ck_assert_str_eq(code, "LS400286");
  xsd_set_free(x.s);
}
END_TEST

START_TEST(attrs_wildcard_union_with_base) {
  cx_t x;
  const bim_wildcard_t *w;
  w = join(&x, "urn:u", "urn:u");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  xsd_set_free(x.s);
  w = join(&x, "##any", "urn:u");
  ck_assert_int_eq(w->kind, BIM_WC_ANY);
  xsd_set_free(x.s);
  w = join(&x, "urn:u", "##any");
  ck_assert_int_eq(w->kind, BIM_WC_ANY);
  xsd_set_free(x.s);
  w = join(&x, "urn:u", "urn:v");
  ck_assert_int_eq(w->kind, BIM_WC_LIST);
  ck_assert_int_eq(w->nns, 2);
  xsd_set_free(x.s);
  w = join(&x, "urn:u urn:v", "urn:v urn:w");
  ck_assert_int_eq(w->nns, 3);
  xsd_set_free(x.s);
  w = join(&x, "##other", "##other");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  xsd_set_free(x.s);
  w = join(&x, "##other", "urn:t");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  ck_assert_ptr_null(w->ns[0]);
  xsd_set_free(x.s);
  w = join(&x, "urn:t", "##other");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  xsd_set_free(x.s);
  w = join(&x, "##other", "urn:t ##local");
  ck_assert_int_eq(w->kind, BIM_WC_ANY);
  xsd_set_free(x.s);
  w = join(&x, "##other", "urn:u");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  ck_assert_ptr_nonnull(w->ns[0]);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(attrs_wildcard_union_not_expressible) {
  cx_t x;
  const char *code;
  const char *docs[1];
  char doc[1024];
  snprintf(doc, sizeof doc,
           DOC_T("<xs:complexType name=\"B\"><xs:anyAttribute namespace=\"##local\"/></xs:complexType>"
                 "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"t:B\"><xs:anyAttribute namespace=\"##other\"/></xs:extension></xs:complexContent></xs:complexType>"));
  docs[0] = doc;
  ck_assert_int_eq(compile(docs, 1, &x, &code), -1);
  ck_assert_str_eq(code, "LS400287");
  xsd_set_free(x.s);
}
END_TEST

START_TEST(attrs_wildcard_union_with_absent_negation) {
  cx_t x;
  const char *code;
  const char *docs[2];
  const bim_wildcard_t *w;
  docs[0] = DOC_N("<xs:complexType name=\"B\"><xs:anyAttribute namespace=\"##other\"/></xs:complexType>"
                  "<xs:complexType name=\"BL\"><xs:anyAttribute namespace=\"##local\"/></xs:complexType>"
                  "<xs:complexType name=\"BU\"><xs:anyAttribute namespace=\"urn:u\"/></xs:complexType>");
  docs[1] = "<xs:schema " XS "xmlns:t=\"urn:t\" targetNamespace=\"urn:t\"><xs:import/>"
            "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"B\"><xs:anyAttribute namespace=\"##other\"/></xs:extension></xs:complexContent></xs:complexType>"
            "</xs:schema>";
  ck_assert_int_eq(compile(docs, 2, &x, &code), 0);
  w = wc_of(&x, "urn:t", "D");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  xsd_set_free(x.s);
  docs[0] = DOC_N("<xs:complexType name=\"B\"><xs:anyAttribute namespace=\"##other\"/></xs:complexType>"
                  "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"B\"><xs:anyAttribute namespace=\"##local\"/></xs:extension></xs:complexContent></xs:complexType>"
                  "<xs:complexType name=\"E\"><xs:complexContent><xs:extension base=\"B\"><xs:anyAttribute namespace=\"urn:u\"/></xs:extension></xs:complexContent></xs:complexType>");
  ck_assert_int_eq(compile(docs, 1, &x, &code), 0);
  w = wc_of(&x, NULL, "D");
  ck_assert_int_eq(w->kind, BIM_WC_ANY);
  w = wc_of(&x, NULL, "E");
  ck_assert_int_eq(w->kind, BIM_WC_NOT);
  xsd_set_free(x.s);
}
END_TEST

static char *chain_doc(int n, int groups) {
  size_t cap = (size_t)n * 200 + 400;
  char *d = calloc(cap, 1);
  size_t at = 0;
  ck_assert_ptr_nonnull(d);
  at += (size_t)snprintf(d + at, cap - at, "<xs:schema " XS "xmlns:t=\"urn:t\" targetNamespace=\"urn:t\">");
  if (groups) {
    for (int i = 0; i < n; i++) {
      if (i + 1 < n) at += (size_t)snprintf(d + at, cap - at, "<xs:attributeGroup name=\"G%d\"><xs:attributeGroup ref=\"t:G%d\"/></xs:attributeGroup>", i, i + 1);
      else at += (size_t)snprintf(d + at, cap - at, "<xs:attributeGroup name=\"G%d\"><xs:anyAttribute/></xs:attributeGroup>", i);
    }
    at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"T\"><xs:attributeGroup ref=\"t:G0\"/></xs:complexType>");
  } else {
    at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C0\"/>");
    for (int i = 1; i < n; i++)
      at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C%d\"><xs:complexContent><xs:extension base=\"t:C%d\"/></xs:complexContent></xs:complexType>", i, i - 1);
  }
  snprintf(d + at, cap - at, "</xs:schema>");
  return d;
}

START_TEST(attrs_nesting_depth_limit) {
  for (int groups = 0; groups < 2; groups++) {
    cx_t x;
    const char *code;
    char *d = chain_doc(300, groups);
    const char *docs[1] = {d};
    xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
    int rc;
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *doc = NULL;
    (void)x;
    ck_assert_int_eq(xml_doc_parse(docs[0], strlen(docs[0]), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, doc, "chain.xsd"), 0);
    rc = xsd_set_resolve(s);
    if (!rc) {
      const xsd_compiled_t *c = NULL;
      rc = xsd_compile(s, &o, &c);
    }
    code = xsd_set_error_code(s);
    ck_assert_int_eq(rc, -1);
    ck_assert_msg(code != NULL, "no code");
    xsd_set_free(s);
    free(d);
  }
}
END_TEST

START_TEST(attrs_compile_allocation_failures) {
  static const char rich[] =
    DOC_T("<xs:attribute name=\"ga\" type=\"xs:string\"/>"
          "<xs:attributeGroup name=\"G\"><xs:attribute ref=\"t:ga\"/><xs:attribute name=\"gb\" type=\"xs:int\" use=\"required\"/><xs:anyAttribute namespace=\"urn:u urn:v\"/></xs:attributeGroup>"
          "<xs:complexType name=\"B\"><xs:attributeGroup ref=\"t:G\"/><xs:anyAttribute namespace=\"##other\"/></xs:complexType>"
          "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"t:B\"><xs:attribute name=\"dd\" type=\"xs:string\"/><xs:anyAttribute namespace=\"urn:w\"/></xs:extension></xs:complexContent></xs:complexType>"
          "<xs:complexType name=\"R\"><xs:complexContent><xs:restriction base=\"t:B\"><xs:attribute name=\"gb\" type=\"xs:int\" use=\"required\"/></xs:restriction></xs:complexContent></xs:complexType>");
  for (long n = 0;; n++) {
    xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    const char *docs[1] = {rich};
    xsd_set_t *s = load(docs, 1);
    int hit;
    alloc_fail_at(n);
    (void)xsd_compile(s, &o, &c);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

static Suite *attrs_suite(void) {
  Suite *s = suite_create("bim_xsd_attrs");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, attrs_wildcard_intersection);
  tcase_add_test(tc, attrs_wildcard_intersection_of_negations);
  tcase_add_test(tc, attrs_wildcard_union_with_base);
  tcase_add_test(tc, attrs_wildcard_union_not_expressible);
  tcase_add_test(tc, attrs_wildcard_union_with_absent_negation);
  tcase_add_test(tc, attrs_nesting_depth_limit);
  tcase_add_test(tc, attrs_compile_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(attrs_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
