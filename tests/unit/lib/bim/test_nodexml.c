/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/nodexml.h"

static int same_s(const char *a, const char *b) { return (!a || !*a) && (!b || !*b) ? 1 : a && b && !strcmp(a, b); }

static int tree_same(const bim_node_t *a, const bim_node_t *b) {
  if (!same_s(a->name, b->name) || !same_s(a->ns, b->ns) || !same_s(a->text, b->text) || a->nattrs != b->nattrs || a->nkids != b->nkids) return 0;
  for (int i = 0; i < a->nattrs; i++) {
    const char *v = bim_node_attr_ns(b, a->attrs[i].ns, a->attrs[i].name);
    if (!v || strcmp(v, a->attrs[i].value)) return 0;
  }
  for (int i = 0; i < a->nkids; i++)
    if (!tree_same(a->kids[i], b->kids[i])) return 0;
  return 1;
}

static bim_node_t *parse(const char *xml, xml_doc_t **doc) {
  ck_assert_int_eq(xml_doc_parse(xml, strlen(xml), doc, NULL), 0);
  return xml_doc_root(*doc);
}

/* parse, write, parse again: same tree, any indentation */
START_TEST(nodexml_round_trips_namespaces_text_and_escapes) {
  static const char xml[] =
    "<a:Root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" a:id=\"1 &amp; 2\" plain=\"x\">"
    "<a:Kid b:att=\"v&quot;q\">text &lt;here&gt;</a:Kid><b:Other/><Free xmlns=\"\">no ns</Free>"
    "<a:Mixed>one<b:Inner>two</b:Inner>three</a:Mixed></a:Root>";
  for (int indent = 0; indent < 3; indent += 2) {
    bim_xml_opts_t o = {NULL, 0, indent, indent};
    xml_doc_t *d1;
    xml_doc_t *d2;
    bim_node_t *r1 = parse(xml, &d1);
    bim_node_t *r2;
    char *text;
    size_t len;
    ck_assert_int_eq(bim_node_to_xml(r1, &o, &text, &len), 0);
    ck_assert_uint_eq(strlen(text), len);
    r2 = parse(text, &d2);
    ck_assert_msg(tree_same(r1, r2), "indent %d: %s", indent, text);
    free(text);
    xml_doc_free(d1);
    xml_doc_free(d2);
  }
}
END_TEST

/* root namespace default, preferred prefixes, generated prefixes, xsi:type from the cast */
START_TEST(nodexml_assigns_prefixes_and_types) {
  static const bim_nsdecl_t pref[] = {{"mpeg7", "urn:mpeg7"}};
  bim_xml_opts_t o = {pref, 1, 0, 0};
  bim_node_t *r = bim_node_new("Main");
  bim_node_t *k;
  bim_type_t t = {.name = "CastType", .ns = "urn:other"};
  char *text;
  r->ns = "urn:main";
  k = bim_node_add_new(r, "Title", "T");
  k->ns = "urn:mpeg7";
  ck_assert_int_eq(bim_node_add_attr_ns(k, "urn:third", "lang", strdup("en")), 0);
  k = bim_node_add_new(r, "Item", NULL);
  k->ns = "urn:main";
  k->cast = &t;
  ck_assert_int_eq(bim_node_to_xml(r, &o, &text, NULL), 0);
  ck_assert_ptr_nonnull(strstr(text, "<Main xmlns=\"urn:main\""));
  ck_assert_ptr_nonnull(strstr(text, "xmlns:mpeg7=\"urn:mpeg7\""));
  ck_assert_ptr_nonnull(strstr(text, "<mpeg7:Title"));
  ck_assert_ptr_nonnull(strstr(text, "xsi:type=\"ns"));
  ck_assert_ptr_nonnull(strstr(text, ":CastType\""));
  ck_assert_ptr_nonnull(strstr(text, "lang=\"en\""));
  free(text);
  bim_node_free(r);
}
END_TEST

typedef struct {
  char buf[256];
  size_t n;
  int puts;
} sink_t;

static void sink_put(void *ctx, const char *s, size_t n) {
  sink_t *k = ctx;
  ck_assert_uint_le(k->n + n, sizeof k->buf - 1);
  memcpy(k->buf + k->n, s, n);
  k->n += n;
  k->buf[k->n] = '\0';
  k->puts++;
}

START_TEST(nodexml_write_streams_through_put_callback) {
  bim_xml_opts_t o = {NULL, 0, 0, 1};
  sink_t k = {{0}, 0, 0};
  bim_node_t *r = bim_node_new("Root");
  bim_node_add_new(r, "Kid", "a<b");
  ck_assert_int_eq(bim_node_write_xml(r, &o, sink_put, &k), 0);
  ck_assert_int_gt(k.puts, 1);
  ck_assert_ptr_eq(strstr(k.buf, "<?xml"), k.buf);
  ck_assert_ptr_nonnull(strstr(k.buf, "<Kid>a&lt;b</Kid>"));
  bim_node_free(r);
}
END_TEST

START_TEST(node_helpers_find_children_and_own_strings) {
  bim_node_t *r = bim_node_new("Root");
  bim_node_t *a = bim_node_add_new(r, "A", "1");
  bim_node_t *b = bim_node_add_new(r, "B", "2");
  const char *kept;
  char tmp[] = "keep me";
  a->ns = "urn:x";
  ck_assert_ptr_eq(bim_node_child(r, "B"), b);
  ck_assert_ptr_eq(bim_node_child(r, "A"), a);
  ck_assert_ptr_null(bim_node_child(r, "C"));
  ck_assert_int_ne(bim_node_is(a, "urn:x", "A"), 0);
  ck_assert_int_eq(bim_node_is(a, "urn:y", "A"), 0);
  ck_assert_int_eq(bim_node_is(a, "urn:x", "B"), 0);
  ck_assert_int_ne(bim_node_is(b, NULL, "B"), 0);
  kept = bim_node_keep(a, tmp);
  ck_assert_ptr_nonnull(kept);
  tmp[0] = 'X';
  ck_assert_str_eq(kept, "keep me");
  bim_node_free(r);
}
END_TEST

/* xml prefix is built in: never declared, never renamed */
START_TEST(nodexml_writes_xml_namespace_attributes_as_xml_prefix) {
  bim_xml_opts_t o = {NULL, 0, 0, 0};
  bim_node_t *r = bim_node_new("Root");
  xml_doc_t *d;
  char *text;
  ck_assert_int_eq(bim_node_add_attr_ns(r, "http://www.w3.org/XML/1998/namespace", "lang", strdup("en")), 0);
  ck_assert_int_eq(bim_node_to_xml(r, &o, &text, NULL), 0);
  ck_assert_ptr_nonnull(strstr(text, " xml:lang=\"en\""));
  ck_assert_ptr_null(strstr(text, "xmlns:"));
  ck_assert_msg(!strstr(text, "ns1"), "%s", text);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &d, NULL), 0);
  xml_doc_free(d);
  free(text);
  bim_node_free(r);
}
END_TEST

static bim_node_t *prefix_tree(void) {
  bim_node_t *r = bim_node_new("Root");
  bim_node_t *k;
  bim_node_t *g;
  static bim_type_t anon = {.name = NULL, .ns = "urn:cast"};
  static bim_type_t named = {.name = "T", .ns = "urn:cast2"};
  r->ns = "urn:main";
  r->nsdecls = calloc(2, sizeof *r->nsdecls);
  ck_assert_ptr_nonnull(r->nsdecls);
  r->nsdecls[0].prefix = "decl";
  r->nsdecls[0].uri = "urn:declared";
  r->nsdecls[1].prefix = "";
  r->nsdecls[1].uri = "urn:defonly";
  r->nnsdecls = 2;
  ck_assert_int_eq(bim_node_add_attr_ns(r, "urn:main", "sameas", strdup("1")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, "urn:main", "again", strdup("2")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, XSI_NS, "nil", strdup("true")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, "urn:xsiclash", "q", strdup("3")), 0);
  k = bim_node_add_new(r, "A", NULL);
  k->ns = "urn:declared";
  g = bim_node_add_new(k, "Deep", "x");
  g->ns = "urn:defonly";
  k = bim_node_add_new(r, "B", NULL);
  k->ns = "urn:pref1";
  k = bim_node_add_new(r, "C", NULL);
  k->ns = "urn:pref2";
  k = bim_node_add_new(r, "D", "text");
  k->cast = &anon;
  bim_node_add_new(k, NULL, "tail");
  g = bim_node_add_new(k, "E", NULL);
  g->cast = &named;
  g->ns = "";
  k = bim_node_add_new(r, "F", NULL);
  k->ns = XML_NS;
  k = bim_node_add_new(r, "Plain", NULL);
  g = bim_node_add_new(k, NULL, NULL);
  (void)g;
  return r;
}

START_TEST(nodexml_prefix_selection_and_layout) {
  static const bim_nsdecl_t pref[] = {{"decl", "urn:pref1"}, {"decl", "urn:pref2"}, {"", "urn:pref2"}, {"xsi", "urn:xsiclash"}, {"p2", "urn:pref2"}};
  for (int np = 0; np < 2; np++)
    for (int indent = 0; indent < 3; indent += 2)
      for (int decl = 0; decl < 2; decl++) {
        bim_xml_opts_t o = {np ? pref : NULL, np ? 5 : 0, indent, decl};
        bim_node_t *r = prefix_tree();
        xml_doc_t *d;
        char *text;
        size_t len;
        ck_assert_int_eq(bim_node_to_xml(r, &o, &text, &len), 0);
        ck_assert_int_eq(xml_doc_parse(text, strlen(text), &d, NULL), 0);
        xml_doc_free(d);
        free(text);
        ck_assert_int_eq(bim_node_to_xml(r, NULL, &text, NULL), 0);
        free(text);
        bim_node_free(r);
      }
}
END_TEST

START_TEST(nodexml_survives_allocation_failures) {
  static const bim_nsdecl_t pref[] = {{"decl", "urn:pref1"}, {"p2", "urn:pref2"}};
  for (int np = 0; np < 2; np++)
    for (long n = 0;; n++) {
      bim_xml_opts_t o = {np ? pref : NULL, np ? 2 : 0, 2, 1};
      bim_node_t *r = prefix_tree();
      char *text = NULL;
      int hit;
      int rc;
      alloc_fail_at(n);
      rc = bim_node_to_xml(r, &o, &text, NULL);
      hit = alloc_fail_end();
      if (!rc) free(text);
      bim_node_free(r);
      if (!hit) break;
    }
}
END_TEST

START_TEST(node_helpers_survive_allocation_failures) {
  bim_node_t *r = bim_node_new("Root");
  bim_node_t *k;
  const char *kept;
  int rc;
  alloc_fail_at(0);
  k = bim_node_new("X");
  alloc_fail_end();
  ck_assert_ptr_null(k);
  alloc_fail_at(0);
  k = bim_node_add_new(r, "A", "text");
  alloc_fail_end();
  ck_assert_ptr_null(k);
  alloc_fail_at(1);
  k = bim_node_add_new(r, "A", "text");
  alloc_fail_end();
  ck_assert_ptr_null(k);
  alloc_fail_at(2);
  k = bim_node_add_new(r, "A", "text");
  alloc_fail_end();
  ck_assert_ptr_null(k);
  ck_assert_int_eq(r->nkids, 0);
  alloc_fail_at(0);
  rc = bim_node_set_text(r, "t");
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  alloc_fail_at(0);
  rc = bim_node_set_attr(r, "a", "v");
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  alloc_fail_at(1);
  rc = bim_node_set_attr(r, "a", "v");
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  ck_assert_int_eq(r->nattrs, 0);
  alloc_fail_at(0);
  rc = bim_node_add_attr_ns(r, "urn:x", "a", strdup("v"));
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  alloc_fail_at(0);
  kept = bim_node_keep(r, "s");
  alloc_fail_end();
  ck_assert_ptr_null(kept);
  alloc_fail_at(1);
  kept = bim_node_keep(r, "s");
  alloc_fail_end();
  ck_assert_ptr_null(kept);
  ck_assert_ptr_nonnull(bim_node_keep(r, "ok"));
  ck_assert_int_eq(bim_node_set_attr(r, "a", "v"), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "a", "w"), 0);
  ck_assert_str_eq(bim_node_attr(r, "a"), "w");
  ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "a", strdup("dup")), -2);
  bim_node_free(r);
}
END_TEST

static Suite *nodexml_suite(void) {
  Suite *s = suite_create("bim_nodexml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, nodexml_round_trips_namespaces_text_and_escapes);
  tcase_add_test(tc, nodexml_assigns_prefixes_and_types);
  tcase_add_test(tc, nodexml_writes_xml_namespace_attributes_as_xml_prefix);
  tcase_add_test(tc, nodexml_write_streams_through_put_callback);
  tcase_add_test(tc, node_helpers_find_children_and_own_strings);
  tcase_add_test(tc, nodexml_prefix_selection_and_layout);
  tcase_add_test(tc, nodexml_survives_allocation_failures);
  tcase_add_test(tc, node_helpers_survive_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(nodexml_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
