/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../alloc_fail.h"
#include "lib/bim/codec.h"
#include "lib/helper/xml_util.h"
#include "lib/helper/xml_util/xml_util_int.h"

START_TEST(xml_escape_escapes_all_five_entities) {
  char *buf;
  size_t len;
  FILE *f = open_memstream(&buf, &len);
  xml_escape(f, "a&b<c>d\"e'f");
  fclose(f);
  ck_assert_str_eq(buf, "a&amp;b&lt;c&gt;d&quot;e&apos;f");
  free(buf);
}
END_TEST

START_TEST(xml_escape_drops_chars_invalid_in_xml_10) {
  char *buf;
  size_t len;
  FILE *f = open_memstream(&buf, &len);
  xml_escape(f, "a\x01" "b\tc\nd\x1f" "e\xEF\xBF\xBE" "f\xEF\xBF\xBF" "g\xC3\xA4");
  fclose(f);
  ck_assert_str_eq(buf, "ab\tc\ndefg\xC3\xA4");
  free(buf);
}
END_TEST

START_TEST(xml_escape_replaces_invalid_utf8) {
  char *buf;
  size_t len;
  FILE *f = open_memstream(&buf, &len);
  xml_escape(f, "a\xFF" "b\xC0\x80" "c\xE2\x82" "d\xED\xA0\x80" "e\xF4\x90\x80\x80" "f\xE2\x82\xAC");
  fclose(f);
  ck_assert_str_eq(buf, "a\xEF\xBF\xBD" "b\xEF\xBF\xBD\xEF\xBF\xBD" "c\xEF\xBF\xBD\xEF\xBF\xBD" "d\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "e\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "f\xE2\x82\xAC");
  free(buf);
}
END_TEST

START_TEST(xml_elem_text_rejects_control_char_refs) {
  static const char doc[] = "<t>a&#x1;b&#9;c&#xFFFE;</t>";
  char out[32];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), 0);
  ck_assert_str_eq(out, "a&#x1;b\tc&#xFFFE;");
}
END_TEST

START_TEST(xml_to_utf8_converts_declared_latin1) {
  static const char doc[] = "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><t>caf\xE9</t>";
  char *buf = strdup(doc);
  size_t len = sizeof doc - 1;
  char enc[32];
  ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), 0);
  ck_assert_str_eq(enc, "ISO-8859-1");
  ck_assert_ptr_nonnull(strstr(buf, "caf\xC3\xA9</t>"));
  ck_assert_uint_eq(len, strlen(buf));
  free(buf);
}
END_TEST

START_TEST(xml_to_utf8_converts_windows_1252) {
  static const char doc[] = "<?xml version='1.0' encoding = 'windows-1252'?><t>\x80\x93x\x94</t>";
  char *buf = strdup(doc);
  size_t len = sizeof doc - 1;
  ck_assert_int_eq(xml_to_utf8(&buf, &len, NULL, 0), 0);
  ck_assert_ptr_nonnull(strstr(buf, "\xE2\x82\xAC\xE2\x80\x9Cx\xE2\x80\x9D</t>"));
  free(buf);
}
END_TEST

START_TEST(xml_to_utf8_leaves_utf8_and_undeclared_alone) {
  static const char a[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?><t>\xC3\xA4</t>";
  static const char b[] = "<t>\xE4</t>";
  char *buf = strdup(a);
  char *orig = buf;
  size_t len = sizeof a - 1;
  ck_assert_int_eq(xml_to_utf8(&buf, &len, NULL, 0), 0);
  ck_assert_ptr_eq(buf, orig);
  free(buf);
  buf = strdup(b);
  orig = buf;
  len = sizeof b - 1;
  ck_assert_int_eq(xml_to_utf8(&buf, &len, NULL, 0), 0);
  ck_assert_ptr_eq(buf, orig);
  free(buf);
}
END_TEST

START_TEST(xml_to_utf8_rejects_unknown_encoding) {
  static const char doc[] = "<?xml version=\"1.0\" encoding=\"no-such-enc\"?><t/>";
  char *buf = strdup(doc);
  size_t len = sizeof doc - 1;
  char enc[32];
  ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), -1);
  ck_assert_str_eq(enc, "no-such-enc");
  ck_assert_str_eq(buf, doc);
  free(buf);
}
END_TEST

START_TEST(xml_attr_extracts_and_decodes_quoted_value) {
  static const char doc[] = "<x id=\"a&amp;b\" other=\"skip\">";
  char out[32];
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "id", out, sizeof out), 0);
  ck_assert_str_eq(out, "a&b");
}
END_TEST

START_TEST(xml_attr_returns_error_when_missing) {
  static const char doc[] = "<x other=\"skip\">";
  char out[32];
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "id", out, sizeof out), -1);
}
END_TEST

START_TEST(xml_elem_text_extracts_and_decodes_body) {
  static const char doc[] = "<title>News &amp; &lt;Weather&gt;</title>";
  char out[64];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "title", out, sizeof out), 0);
  ck_assert_str_eq(out, "News & <Weather>");
}
END_TEST

START_TEST(xml_elem_text_decodes_numeric_char_refs) {
  static const char doc[] = "<t>&#65;&#x42;</t>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), 0);
  ck_assert_str_eq(out, "AB");
}
END_TEST

START_TEST(xml_elem_text_rejects_self_closing_tag) {
  static const char doc[] = "<t/>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), -1);
}
END_TEST

START_TEST(xml_elem_text_does_not_match_prefix_tag_name) {
  /* "t" must not match inside "<title>" */
  static const char doc[] = "<title>hello</title>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), -1);
}
END_TEST

START_TEST(xml_elem_text_truncation_keeps_utf8_whole) {
  static const char doc[] = "<d>aaaa\xC3\xA4" "b</d>";
  char out[6];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "d", out, sizeof out), 0);
  ck_assert_str_eq(out, "aaaa");
}
END_TEST

START_TEST(xml_elem_text_chk_reports_truncation) {
  static const char doc[] = "<d>abcdefgh</d>";
  char big[16];
  char out[6];
  int cut = 0;
  ck_assert_int_eq(xml_elem_text_chk(doc, doc + strlen(doc), "d", out, sizeof out, &cut), 0);
  ck_assert_int_eq(cut, 1);
  ck_assert_str_eq(out, "abcde");
  cut = 1;
  ck_assert_int_eq(xml_elem_text_chk(doc, doc + strlen(doc), "d", big, sizeof big, &cut), 0);
  ck_assert_int_eq(cut, 0);
}
END_TEST

START_TEST(xml_elem_text_dup_returns_full_decoded_text) {
  static const char doc[] = "<d>a &amp; <![CDATA[<b>]]> &#228;</d><e/>";
  char *out = NULL;
  ck_assert_int_eq(xml_elem_text_dup(doc, doc + strlen(doc), "d", &out), 0);
  ck_assert_str_eq(out, "a & <b> \xC3\xA4");
  free(out);
  ck_assert_int_eq(xml_elem_text_dup(doc, doc + strlen(doc), "e", &out), -1);
  ck_assert_int_eq(xml_elem_text_dup(doc, doc + strlen(doc), "x", &out), -1);
}
END_TEST

START_TEST(xml_elem_text_dup_keeps_text_past_1k) {
  char doc[4096];
  char *out = NULL;
  size_t n = 0;
  n += (size_t)snprintf(doc + n, sizeof doc - n, "<d>");
  memset(doc + n, 'x', 3000);
  n += 3000;
  snprintf(doc + n, sizeof doc - n, "</d>");
  ck_assert_int_eq(xml_elem_text_dup(doc, doc + strlen(doc), "d", &out), 0);
  ck_assert_uint_eq(strlen(out), 3000u);
  free(out);
}
END_TEST

START_TEST(xml_attr_chk_reports_truncation) {
  static const char doc[] = "<a id=\"abcdefgh\">";
  char big[16];
  char out[6];
  int cut = 0;
  ck_assert_int_eq(xml_attr_chk(doc, doc + strlen(doc), "id", out, sizeof out, &cut), 0);
  ck_assert_int_eq(cut, 1);
  ck_assert_str_eq(out, "abcde");
  cut = 1;
  ck_assert_int_eq(xml_attr_chk(doc, doc + strlen(doc), "id", big, sizeof big, &cut), 0);
  ck_assert_int_eq(cut, 0);
}
END_TEST

START_TEST(xml_attr_accepts_single_quotes_and_spaced_equals) {
  static const char doc[] = "<x a = 'one' b\t=\n\"two\" c='say \"hi\"'>";
  char out[32];
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "a", out, sizeof out), 0);
  ck_assert_str_eq(out, "one");
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "b", out, sizeof out), 0);
  ck_assert_str_eq(out, "two");
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "c", out, sizeof out), 0);
  ck_assert_str_eq(out, "say \"hi\"");
}
END_TEST

START_TEST(xml_attr_ignores_longer_names_text_and_comments) {
  static const char doc[] = "<!-- <y id=\"c\"> -->text id=\"t\"<x tsid=\"1\" xml:id=\"p\" id=\"2\">";
  char out[32];
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "id", out, sizeof out), 0);
  ck_assert_str_eq(out, "p");
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "sid", out, sizeof out), -1);
}
END_TEST

START_TEST(xml_tag_attr_looks_only_at_the_start_tag) {
  static const char doc[] = "<a x=\"1\"><b y=\"2\"/></a>";
  char out[8];
  ck_assert_int_eq(xml_tag_attr(doc, doc + strlen(doc), "x", out, sizeof out), 0);
  ck_assert_str_eq(out, "1");
  ck_assert_int_eq(xml_tag_attr(doc, doc + strlen(doc), "y", out, sizeof out), -1);
  ck_assert_int_eq(xml_attr(doc, doc + strlen(doc), "y", out, sizeof out), 0);
}
END_TEST

START_TEST(xml_attr_list_reads_bare_attribute_text) {
  static const char txt[] = "-1 tvg-logo=\"http://x/l.png\" tsid='7'";
  char out[32];
  ck_assert_int_eq(xml_attr_list(txt, txt + strlen(txt), "tvg-logo", out, sizeof out), 0);
  ck_assert_str_eq(out, "http://x/l.png");
  ck_assert_int_eq(xml_attr_list(txt, txt + strlen(txt), "tsid", out, sizeof out), 0);
  ck_assert_str_eq(out, "7");
}
END_TEST

START_TEST(xml_elem_text_unwraps_cdata) {
  static const char doc[] = "<title>a <![CDATA[Tom & <Jerry>]]> b &amp; c</title>";
  char out[64];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "title", out, sizeof out), 0);
  ck_assert_str_eq(out, "a Tom & <Jerry> b & c");
}
END_TEST

START_TEST(xml_elem_text_matches_namespace_prefix) {
  static const char doc[] = "<tva:Title type='main'>Hello</tva:Title>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "Title", out, sizeof out), 0);
  ck_assert_str_eq(out, "Hello");
}
END_TEST

START_TEST(xml_elem_text_skips_comments_and_child_markup) {
  static const char doc[] = "<t><!-- </t> -->x<b>y</b><?pi z?>w</t>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), 0);
  ck_assert_str_eq(out, "xyw");
}
END_TEST

START_TEST(xml_elem_text_handles_same_name_nesting) {
  static const char doc[] = "<n>a<n>b</n>c</n>";
  char out[16];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "n", out, sizeof out), 0);
  ck_assert_str_eq(out, "abc");
}
END_TEST

START_TEST(xml_numeric_ref_rejects_invalid_code_points) {
  static const char doc[] = "<t>&#0;&#xD800;&#x110000;&#xzz;</t>";
  char out[64];
  ck_assert_int_eq(xml_elem_text(doc, doc + strlen(doc), "t", out, sizeof out), 0);
  ck_assert_str_eq(out, "&#0;&#xD800;&#x110000;&#xzz;");
}
END_TEST

typedef struct {
  int n;
  char names[4][8];
} each_t;

static int each_cb(const char *tag, const char *blk_end, void *ctx) {
  each_t *e = ctx;
  if (e->n < 4) {
    if (xml_tag_attr(tag, blk_end, "id", e->names[e->n], sizeof e->names[0])) e->names[e->n][0] = '\0';
    e->n++;
  }
  return 0;
}

START_TEST(for_each_xml_elem_visits_elements_by_local_name) {
  static const char doc[] = "<r><i id='a'/><x:i id=\"b\">t</x:i><ii id='no'/><i id = 'c'><i id='nested'/></i></r>";
  each_t e = {0};
  ck_assert_int_eq(for_each_xml_elem(doc, doc + strlen(doc), "i", each_cb, &e), 0);
  ck_assert_int_eq(e.n, 3);
  ck_assert_str_eq(e.names[0], "a");
  ck_assert_str_eq(e.names[1], "b");
  ck_assert_str_eq(e.names[2], "c");
}
END_TEST

START_TEST(for_each_xml_elem_ignores_unclosed_trailing_element) {
  static const char doc[] = "<i id='a'/><i id='b'>text";
  each_t e = {0};
  ck_assert_int_eq(for_each_xml_elem(doc, doc + strlen(doc), "i", each_cb, &e), 0);
  ck_assert_int_eq(e.n, 1);
}
END_TEST

START_TEST(for_each_xml_elem_chk_reports_unclosed_element) {
  static const char doc[] = "<i id='a'/><i id='b'>text<i id='c'/>";
  each_t e = {0};
  const char *bad = NULL;
  ck_assert_int_eq(for_each_xml_elem_chk(doc, doc + strlen(doc), "i", each_cb, &e, &bad), 0);
  ck_assert_int_eq(e.n, 1);
  ck_assert_ptr_eq(bad, doc + 11);
}
END_TEST

START_TEST(for_each_xml_elem_chk_leaves_unclosed_alone_when_balanced) {
  static const char doc[] = "<i id='a'/><i id='b'>t</i>";
  each_t e = {0};
  const char *bad = NULL;
  ck_assert_int_eq(for_each_xml_elem_chk(doc, doc + strlen(doc), "i", each_cb, &e, &bad), 0);
  ck_assert_int_eq(e.n, 2);
  ck_assert_ptr_null(bad);
}
END_TEST

START_TEST(xml_find_start_needs_no_close_tag) {
  static const char doc[] = "<?xml version='1.0'?><!DOCTYPE a [<!ENTITY e \">\">]><sd:Root a='1'>";
  ck_assert_ptr_nonnull(xml_find_start(doc, doc + strlen(doc), "Root"));
  ck_assert_ptr_null(xml_find_start(doc, doc + strlen(doc), "Ro"));
}
END_TEST

static xml_doc_t *parse_ok(const char *xml) {
  xml_doc_t *doc = NULL;
  ck_assert_int_eq(xml_doc_parse(xml, strlen(xml), &doc, NULL), 0);
  return doc;
}

static const char *attr_ns(const bim_node_t *n, const char *name, const char **ns) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name)) {
      *ns = n->attrs[i].ns;
      return n->attrs[i].value;
    }
  return NULL;
}

START_TEST(xml_doc_resolves_namespaces_and_decodes) {
  static const char xml[] = "\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><!DOCTYPE r>"
                            "<r xmlns=\"urn:a\" xmlns:b=\"urn:b\" x=\"1\" b:y=\"&lt;2&#65;\">"
                            "<b:c>t&amp;x</b:c><d/><e><![CDATA[<z>]]></e><f xmlns=\"\"/></r>";
  xml_doc_t *doc = parse_ok(xml);
  const bim_node_t *r = xml_doc_root(doc);
  const char *ns = "set";
  ck_assert_str_eq(r->name, "r");
  ck_assert_str_eq(r->ns, "urn:a");
  ck_assert_str_eq(attr_ns(r, "x", &ns), "1");
  ck_assert_ptr_null(ns);
  ck_assert_str_eq(attr_ns(r, "y", &ns), "<2A");
  ck_assert_str_eq(ns, "urn:b");
  ck_assert_int_eq(r->nkids, 4);
  ck_assert_str_eq(r->kids[0]->name, "c");
  ck_assert_str_eq(r->kids[0]->ns, "urn:b");
  ck_assert_str_eq(r->kids[0]->text, "t&x");
  ck_assert_str_eq(r->kids[1]->ns, "urn:a");
  ck_assert_ptr_null(r->kids[1]->text);
  ck_assert_str_eq(r->kids[2]->text, "<z>");
  ck_assert_ptr_null(r->kids[3]->ns);
  ck_assert_ptr_eq(r->kids[0]->parent, r);
  ck_assert_str_eq(bim_node_ns_uri(r->kids[0], "b"), "urn:b");
  ck_assert_str_eq(bim_node_ns_uri(r->kids[0], "xml"), "http://www.w3.org/XML/1998/namespace");
  ck_assert_ptr_null(bim_node_ns_uri(r->kids[3], ""));
  xml_doc_free(doc);
}
END_TEST

/* mixed content text nodes, whitespace between elements dropped */
START_TEST(xml_doc_keeps_mixed_text) {
  xml_doc_t *doc = parse_ok("<r>a<k/>b</r>");
  const bim_node_t *r = xml_doc_root(doc);
  ck_assert_int_eq(r->nkids, 3);
  ck_assert_ptr_null(r->kids[0]->name);
  ck_assert_str_eq(r->kids[0]->text, "a");
  ck_assert_str_eq(r->kids[1]->name, "k");
  ck_assert_str_eq(r->kids[2]->text, "b");
  ck_assert_ptr_null(r->text);
  xml_doc_free(doc);
  doc = parse_ok("<r>\n  <k/>\n</r>");
  ck_assert_int_eq(xml_doc_root(doc)->nkids, 1);
  xml_doc_free(doc);
  doc = parse_ok("<r> x </r>");
  ck_assert_str_eq(xml_doc_root(doc)->text, " x ");
  xml_doc_free(doc);
}
END_TEST

/* XML 1.0 2.11, 3.3.3 */
START_TEST(xml_doc_normalizes_line_ends_and_attr_space) {
  const char *ns;
  xml_doc_t *doc = parse_ok("<r a=\"x\r\ny\tz&#10;\">l1\r\nl2\rl3</r>");
  const bim_node_t *r = xml_doc_root(doc);
  ck_assert_str_eq(attr_ns(r, "a", &ns), "x y z\n");
  ck_assert_str_eq(r->text, "l1\nl2\nl3");
  xml_doc_free(doc);
}
END_TEST

START_TEST(xml_doc_rejects_malformed) {
  static const struct {
    const char *xml;
    size_t off;
  } cases[] = {
    {"<r><p:x/></r>", 3},
    {"<r><a></b></r>", 6},
    {"<r a=\"1\" a=\"2\"/>", 0},
    {"<r xmlns:p=\"u\" xmlns:q=\"u\" p:a=\"1\" q:a=\"2\"/>", 0},
    {"<r/><s/>", 4},
    {"x<r/>", 0},
    {"<r><!-- x </r>", 3},
    {"<r>", 3},
    {"<r a=1/>", 5},
    {"<r xmlns:p=\"\"/>", 0},
    {"<r><!DOCTYPE r></r>", 3},
    {"", 0},
  };
  for (size_t c = 0; c < sizeof cases / sizeof cases[0]; c++) {
    xml_doc_t *doc = NULL;
    size_t off = 99;
    ck_assert_int_eq(xml_doc_parse(cases[c].xml, strlen(cases[c].xml), &doc, &off), -1);
    ck_assert_ptr_null(doc);
    ck_assert_uint_eq(off, cases[c].off);
  }
}
END_TEST

START_TEST(xml_doc_rejects_more_malformed_forms) {
  static const char *const cases[] = {
    "<", "<r", "<r ", "<r a", "<r a=", "<r a=\"1", "<r a=\"1\"b=\"2\"/>", "<r a=\"<\"/>", "<r =\"1\"/>", "</r>", "<r></r", "<r></ r>", "<r></r x>", "<r/",
    "<r a='1\"/>", "<r><![CDATA[x</r>", "<![CDATA[x]]>", "<r><?pi</r>", "<?pi ?", "<!-- c", "<r><!-- c --", "<!DOCTYPE r", "<r>&bogus;</r>", "<:r/>", "<r:/>", "<r:s:t/>",
    "<r a:b:c=\"1\"/>", "<r :a=\"1\"/>", "<r xmlns:p=\"u\" p:/>", "<r/>x", "<r/><!DOCTYPE r>", "<r><r2></r></r2>", "<r\t\n/ >", "<r a=\"1\" / >",
  };
  for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
    xml_doc_t *doc = NULL;
    int rc = xml_doc_parse(cases[c], strlen(cases[c]), &doc, NULL);
    if (!rc) {
      xml_doc_free(doc);
      continue;
    }
    ck_assert_int_eq(rc, -1);
  }
}
END_TEST

START_TEST(xml_doc_accepts_every_markup_form) {
  static const char text[] =
    "\xEF\xBB\xBF<?xml version=\"1.0\"?><!DOCTYPE r><!-- c --><r xmlns=\"urn:d\" xmlns:p=\"urn:p\" p:a=\"1\" b='2'>\r\n  <p:k>text &amp; more</p:k><![CDATA[raw <b> \r\n]]><k2 xmlns=\"\"/><?pi data?>mixed<!-- x -->tail</r><!-- end -->\n";
  xml_doc_t *doc = NULL;
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  ck_assert_ptr_nonnull(xml_doc_root(doc));
  xml_doc_free(doc);
  xml_doc_free(NULL);
}
END_TEST

START_TEST(xml_doc_grows_its_name_table_and_limits_depth) {
  size_t cap = 400 * 24 + 64;
  char *big = calloc(cap, 1);
  char *deep = calloc(300 * 8 + 64, 1);
  size_t at = 0;
  xml_doc_t *doc = NULL;
  ck_assert_ptr_nonnull(big);
  ck_assert_ptr_nonnull(deep);
  at += (size_t)snprintf(big + at, cap - at, "<root>");
  for (int i = 0; i < 400; i++) at += (size_t)snprintf(big + at, cap - at, "<n%d a%d=\"v\"/>", i, i);
  snprintf(big + at, cap - at, "</root>");
  ck_assert_int_eq(xml_doc_parse(big, strlen(big), &doc, NULL), 0);
  xml_doc_free(doc);
  at = 0;
  for (int i = 0; i < 300; i++) at += (size_t)snprintf(deep + at, 16, "<d>");
  for (int i = 0; i < 300; i++) at += (size_t)snprintf(deep + at, 16, "</d>");
  doc = NULL;
  ck_assert_int_eq(xml_doc_parse(deep, strlen(deep), &doc, NULL), -1);
  free(big);
  free(deep);
}
END_TEST

START_TEST(xml_doc_parse_survives_allocation_failures) {
  static const char text[] = "<r xmlns=\"urn:d\" xmlns:p=\"urn:p\" p:a=\"1\" b=\"2\">\n  <p:k>text &amp; more</p:k><![CDATA[raw]]><k2 a=\"1\"/>mixed<k3>deep<k4/></k3>tail</r>";
  size_t cap = 500 * 24 + 64;
  char *big = calloc(cap, 1);
  size_t at = 0;
  at += (size_t)snprintf(big + at, cap - at, "<root xmlns:q=\"urn:q\">");
  for (int i = 0; i < 300; i++) at += (size_t)snprintf(big + at, cap - at, "<q:n%d a%d=\"v\"/>", i, i);
  snprintf(big + at, cap - at, "</root>");
  for (int which = 0; which < 2; which++) {
    const char *src = which ? big : text;
    for (long n = 0;; n++) {
      xml_doc_t *doc = NULL;
      int hit;
      int rc;
      alloc_fail_at(n);
      rc = xml_doc_parse(src, strlen(src), &doc, NULL);
      hit = alloc_fail_end();
      if (!rc) xml_doc_free(doc);
      if (!hit) break;
    }
  }
  free(big);
}
END_TEST

START_TEST(xml_to_utf8_grows_its_buffer_and_survives_allocation_failure) {
  static const char head[] = "<?xml version=\"1.0\" encoding=\"windows-1252\"?><t>";
  char doc[512];
  size_t hl = sizeof head - 1;
  size_t total;
  memcpy(doc, head, hl);
  memset(doc + hl, 0x80, 200);
  memcpy(doc + hl + 200, "</t>", 4);
  total = hl + 204;
  {
    char *buf = malloc(total + 1);
    size_t len = total;
    ck_assert_ptr_nonnull(buf);
    memcpy(buf, doc, total);
    buf[total] = 0;
    ck_assert_int_eq(xml_to_utf8(&buf, &len, NULL, 0), 0);
    ck_assert_uint_eq(len, strlen(buf));
    ck_assert_uint_gt(len, total * 2);
    free(buf);
  }
  for (long n = 0;; n++) {
    char *buf = malloc(total + 1);
    size_t len = total;
    int hit;
    int rc;
    ck_assert_ptr_nonnull(buf);
    memcpy(buf, doc, total);
    buf[total] = 0;
    alloc_fail_at(n);
    rc = xml_to_utf8(&buf, &len, NULL, 0);
    hit = alloc_fail_end();
    free(buf);
    if (!hit) {
      ck_assert_int_eq(rc, 0);
      break;
    }
    ck_assert_int_eq(rc, -2);
  }
}
END_TEST

START_TEST(xml_to_utf8_reads_declarations_strictly) {
  static const char *const plain[] = {
    "<?xml version=\"1.0\"?><t/>",
    "<?xml version=\"1.0\" encoding?><t/>",
    "<?xml version=\"1.0\" encoding=latin1?><t/>",
    "<?xml version=\"1.0\" encoding=\"\"?><t/>",
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1><t/>",
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"",
    "<?xml version=\"1.0\" encoding=",
    "<?xml version=\"1.0\" encoding =",
    "<?xmlx version=\"1.0\" encoding=\"ISO-8859-1\"?><t/>",
    "<?xml",
    "",
    "<t/>",
    "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?><t/>",
    "<?xml version=\"1.0\" encoding=\"us-ascii\"?><t/>",
    "<?xml version=\"1.0\" encoding=\"UTF8\"?><t/>",
    "<?xml version=\"1.0\" encoding=\"ASCII\"?><t/>",
  };
  char enc[8];
  for (size_t i = 0; i < sizeof plain / sizeof *plain; i++) {
    char *buf = strdup(plain[i]);
    size_t len = strlen(plain[i]);
    ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), 0);
    ck_assert_str_eq(buf, plain[i]);
    free(buf);
  }
  {
    char *buf = strdup("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?><t>\xE9</t>");
    ck_assert_ptr_nonnull(buf);
    size_t len = strlen(buf);
    ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), 0);
    ck_assert_str_eq(enc, "ISO-885");
    free(buf);
  }
  {
    char *buf = strdup("<?xml version=\"1.0\" encoding=\"UTF-16LE\"?><t/>");
    ck_assert_ptr_nonnull(buf);
    size_t len = strlen(buf);
    ck_assert_int_le(xml_to_utf8(&buf, &len, NULL, 0), 0);
    free(buf);
  }
  {
    static const char bad[] = "<?xml version=\"1.0\" encoding=\"UTF-16LE\"?>x\x00\xD8";
    size_t len = sizeof bad - 1;
    char *buf = malloc(len + 1);
    ck_assert_ptr_nonnull(buf);
    memcpy(buf, bad, len + 1);
    ck_assert_int_eq(xml_to_utf8(&buf, &len, NULL, 0), -1);
    free(buf);
  }
  {
    char longname[200];
    char text[300];
    char *buf;
    size_t len;
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = 0;
    snprintf(text, sizeof text, "<?xml version=\"1.0\" encoding=\"%s\"?><t/>", longname);
    buf = strdup(text);
    len = strlen(text);
    ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), 0);
    ck_assert_str_eq(buf, text);
    free(buf);
    longname[40] = 0;
    snprintf(text, sizeof text, "<?xml version=\"1.0\" encoding=\"%s\"?><t/>", longname);
    buf = strdup(text);
    len = strlen(text);
    ck_assert_int_eq(xml_to_utf8(&buf, &len, enc, sizeof enc), -1);
    free(buf);
  }
}
END_TEST

START_TEST(xml_scan_handles_odd_markup) {
  static const char decl_open[] = "<!DOCTYPE x [ <!ENTITY a \"b]\"> ";
  static const char bare_lt[] = "< x><a>t</a>";
  static const char tail_lt[] = "<a>t</a><";
  static const char cdata_open[] = "<a><![CDATA[never closed";
  static const char comment_open[] = "<a><!-- never closed";
  static const char pi_open[] = "<a><?pi never closed";
  static const char quoted[] = "<a b=\"x>y\" c='z>w'>t</a>";
  xml_tok_t t;
  xml_tok_t c;
  const char *q;
  char out[32];
  int trunc = 0;
  sink_t k;
  q = skip_misc(decl_open, decl_open + sizeof decl_open - 1);
  ck_assert_ptr_eq(q, decl_open + sizeof decl_open - 1);
  ck_assert_int_eq(next_tok(decl_open, decl_open + sizeof decl_open - 1, &t), -1);
  ck_assert_int_eq(next_tok(bare_lt, bare_lt + sizeof bare_lt - 1, &t), 0);
  ck_assert_int_eq(t.nlen, 1);
  ck_assert_int_eq(next_tok(tail_lt, tail_lt + sizeof tail_lt - 1, &t), 0);
  ck_assert_int_eq(next_tok(tail_lt + 3, tail_lt + sizeof tail_lt - 1, &t), 0);
  ck_assert_int_eq(next_tok("<", "<" + 1, &t), -1);
  ck_assert_int_eq(next_tok("", "", &t), -1);
  ck_assert_int_eq(next_tok("<>", "<>" + 2, &t), -1);
  ck_assert_int_eq(next_tok(cdata_open + 3, cdata_open + sizeof cdata_open - 1, &t), -1);
  ck_assert_int_eq(next_tok(comment_open + 3, comment_open + sizeof comment_open - 1, &t), -1);
  ck_assert_int_eq(next_tok(pi_open + 3, pi_open + sizeof pi_open - 1, &t), -1);
  ck_assert_int_eq(next_tok(quoted, quoted + sizeof quoted - 1, &t), 0);
  ck_assert_ptr_eq(t.gt, quoted + 18);
  ck_assert_ptr_eq(tok_after(&t, quoted + sizeof quoted - 1), quoted + 19);
  ck_assert_int_eq(find_close(&t, quoted + sizeof quoted - 1, &c), 0);
  ck_assert_int_eq(c.close, 1);
  ck_assert_int_eq(next_tok("<a", "<a" + 2, &t), 0);
  ck_assert_ptr_eq(tok_after(&t, "<a" + 2), "<a" + 2);
  ck_assert_int_eq(find_close(&t, "<a" + 2, &c), -1);
  ck_assert_int_eq(name_is("ns:Name", 7, "Name"), 1);
  ck_assert_int_eq(name_is("Name", 4, "Name"), 1);
  ck_assert_int_eq(name_is("ns:Other", 8, "Name"), 0);
  ck_assert_int_eq(name_is(":", 1, "x"), 0);
  ck_assert_int_eq(attr_lookup(quoted + 2, quoted + 18, "b", out, sizeof out, &trunc), 0);
  ck_assert_str_eq(out, "x>y");
  ck_assert_int_eq(attr_lookup(quoted + 2, quoted + 18, "zz", out, sizeof out, &trunc), -1);
  ck_assert_int_eq(attr_lookup(" /  = 'v' a b='open", " /  = 'v' a b='open" + 19, "a", out, sizeof out, &trunc), -1);
  ck_assert_int_eq(attr_lookup("a = v", "a = v" + 5, "a", out, sizeof out, &trunc), -1);
  ck_assert_int_eq(attr_lookup("a", "a" + 1, "a", out, sizeof out, &trunc), -1);
  sink_init(&k, out, sizeof out);
  gather_text(cdata_open, cdata_open + sizeof cdata_open - 1, &k);
  sink_finish(&k, &trunc);
  ck_assert_str_eq(out, "never closed");
  sink_init(&k, out, sizeof out);
  gather_text("a<!-- c -->b<?pi x?>c<![CDATA[d]]>e<f>g</f>", "a<!-- c -->b<?pi x?>c<![CDATA[d]]>e<f>g</f>" + 43, &k);
  sink_finish(&k, &trunc);
  ck_assert_str_eq(out, "abcdeg");
  sink_init(&k, out, sizeof out);
  gather_text("a < b", "a < b" + 5, &k);
  sink_finish(&k, &trunc);
  ck_assert_ptr_nonnull(strstr(out, "a"));
}
END_TEST

typedef struct {
  char buf[256];
  size_t n;
} esc_sink_t;

static void esc_put(void *ctx, const char *s, size_t n) {
  esc_sink_t *k = ctx;
  memcpy(k->buf + k->n, s, n);
  k->n += n;
  k->buf[k->n] = 0;
}

START_TEST(xml_escape_covers_every_character_class) {
  static const struct {
    const char *in;
    const char *want;
  } cases[] = {
    {"&<>\"'", "&amp;&lt;&gt;&quot;&apos;"},
    {"a\tb\nc\rd", "a\tb\nc\rd"},
    {"\x01\x1F x", " x"},
    {"\xC3\xA4", "\xC3\xA4"},
    {"\xE2\x82\xAC", "\xE2\x82\xAC"},
    {"\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80"},
    {"\xC0\x80", "\xEF\xBF\xBD\xEF\xBF\xBD"},
    {"\x80", "\xEF\xBF\xBD"},
    {"\xFF", "\xEF\xBF\xBD"},
    {"\xE0\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"},
    {"\xF0\x80\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"},
    {"\xF4\x90\x80\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"},
    {"\xED\xA0\x80", "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD"},
    {"\xE2\x41\xAC", "\xEF\xBF\xBD" "A" "\xEF\xBF\xBD"},
    {"\xC3", "\xEF\xBF\xBD"},
    {"\xEF\xBF\xBE", ""},
    {"\xEF\xBF\xBF", ""},
    {"\xEF\xBF\xBD", "\xEF\xBF\xBD"},
  };
  char enc[5];
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    esc_sink_t k = {{0}, 0};
    xml_escape_to(cases[i].in, esc_put, &k);
    ck_assert_msg(!strcmp(k.buf, cases[i].want), "case %zu", i);
  }
  ck_assert_int_eq(utf8_encode(0x41, enc), 1);
  ck_assert_int_eq(utf8_encode(0x7FF, enc), 2);
  ck_assert_int_eq(utf8_encode(0xFFFF, enc), 3);
  ck_assert_int_eq(utf8_encode(0x10FFFF, enc), 4);
  {
    FILE *f = tmpfile();
    char back[32];
    ck_assert_ptr_nonnull(f);
    xml_escape(f, "a<b");
    rewind(f);
    ck_assert_ptr_nonnull(fgets(back, sizeof back, f));
    ck_assert_str_eq(back, "a&lt;b");
    fclose(f);
  }
}
END_TEST

START_TEST(xml_sink_decodes_entities_and_cuts_on_sequence_boundaries) {
  static const struct {
    const char *in;
    const char *want;
  } cases[] = {
    {"a&amp;b&lt;&gt;&quot;&apos;", "a&b<>\"'"}, {"&#65;&#x42;&#X43;", "ABC"}, {"&#9;&#10;&#13;", "\t\n\r"}, {"&#xE9;&#x20AC;&#x1F600;", "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"},
    {"&#1;", "&#1;"}, {"&#xD800;", "&#xD800;"}, {"&#xFFFE;", "&#xFFFE;"}, {"&#x110000;", "&#x110000;"}, {"&#;", "&#;"}, {"&#xg;", "&#xg;"},
    {"&#65", "&#65"}, {"&#65x;", "&#65x;"}, {"&unknown;", "&unknown;"}, {"&", "&"}, {"&#", "&#"}, {"&am", "&am"}, {"plain", "plain"},
  };
  char out[64];
  sink_t k;
  int trunc = 9;
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    sink_init(&k, out, sizeof out);
    sink_decode(&k, cases[i].in, strlen(cases[i].in));
    ck_assert_int_eq(sink_finish(&k, &trunc), 0);
    ck_assert_int_eq(trunc, 0);
    ck_assert_msg(!strcmp(out, cases[i].want), "case %zu: '%s' want '%s'", i, out, cases[i].want);
  }
  sink_init(&k, out, 6);
  sink_decode(&k, "abcdefgh", 8);
  ck_assert_int_eq(sink_finish(&k, &trunc), 1);
  ck_assert_int_eq(trunc, 1);
  ck_assert_str_eq(out, "abcde");
  for (size_t cap = 1; cap < 9; cap++) {
    sink_init(&k, out, cap);
    sink_decode(&k, "\xC3\xA4\xE2\x82\xAC\xF0\x9F\x98\x80", 9);
    (void)sink_finish(&k, NULL);
    ck_assert_msg(strlen(out) < cap, "cap %zu", cap);
  }
  sink_init(&k, out, 2);
  sink_put(&k, "xy", 2);
  ck_assert_int_eq(sink_finish(&k, NULL), 1);
  sink_init(&k, out, 16);
  sink_put(&k, "\xE2\x82", 2);
  k.cut = 1;
  (void)sink_finish(&k, NULL);
  ck_assert_str_eq(out, "");
  sink_init(&k, out, 16);
  sink_put(&k, "ab\xF0\x9F", 4);
  k.cut = 1;
  (void)sink_finish(&k, NULL);
  ck_assert_str_eq(out, "ab");
  sink_init(&k, out, 16);
  sink_put(&k, "\x80\x80\x80\x80", 4);
  k.cut = 1;
  (void)sink_finish(&k, NULL);
}
END_TEST

static int count_cb(const char *tag, const char *blk_end, void *ctx) {
  (void)tag;
  (void)blk_end;
  return ++*(int *)ctx >= 3 ? 1 : 0;
}

START_TEST(xml_query_api_covers_each_path) {
  static const char doc[] = "<r a='1'><x k=\"v\">t&amp;1</x><x/><y>skip</y><x>third</x><p:x>pre</p:x><u>open";
  const char *end = doc + sizeof doc - 1;
  xml_span_t sp;
  char out[32];
  char *dup = NULL;
  int trunc = 0;
  int count = 0;
  const char *unclosed = NULL;
  ck_assert_ptr_nonnull(xml_find_start(doc, end, "x"));
  ck_assert_ptr_null(xml_find_start(doc, end, "zzz"));
  ck_assert_int_eq(xml_find_elem(doc, end, "x", &sp), 0);
  ck_assert_int_eq(xml_find_elem(doc, end, "u", &sp), -1);
  ck_assert_int_eq(xml_find_elem(doc, end, "zzz", &sp), -1);
  ck_assert_int_eq(xml_find_elem("<x/>", "<x/>" + 4, "x", &sp), 0);
  ck_assert_int_eq(xml_elem_text(doc, end, "y", out, sizeof out), 0);
  ck_assert_str_eq(out, "skip");
  ck_assert_int_eq(xml_elem_text_chk(doc, end, "y", out, 3, &trunc), 0);
  ck_assert_int_eq(trunc, 1);
  ck_assert_int_eq(xml_elem_text(doc, end, "zzz", out, sizeof out), -1);
  ck_assert_int_eq(xml_elem_text("<x/>", "<x/>" + 4, "x", out, sizeof out), -1);
  ck_assert_int_eq(xml_elem_text_dup(doc, end, "x", &dup), 0);
  ck_assert_str_eq(dup, "t&1");
  free(dup);
  ck_assert_int_eq(xml_elem_text_dup(doc, end, "zzz", &dup), -1);
  ck_assert_int_eq(xml_elem_text_dup("<x/>", "<x/>" + 4, "x", &dup), -1);
  ck_assert_int_eq(xml_span_text_chk("</x>", "</x>" + 4, out, sizeof out, NULL), -1);
  ck_assert_int_eq(xml_span_text_chk("plain", "plain" + 5, out, sizeof out, NULL), -1);
  ck_assert_int_eq(for_each_xml_elem(doc, end, "x", count_cb, &count), -1);
  ck_assert_int_eq(count, 3);
  count = 0;
  ck_assert_int_eq(for_each_xml_elem_chk(doc, end, "u", count_cb, &count, &unclosed), 0);
  ck_assert_ptr_nonnull(unclosed);
  ck_assert_int_eq(for_each_xml_elem_chk(doc, end, "u", count_cb, &count, NULL), 0);
  ck_assert_int_eq(xml_attr(doc, end, "k", out, sizeof out), 0);
  ck_assert_str_eq(out, "v");
  ck_assert_int_eq(xml_attr_chk(doc, end, "k", out, sizeof out, &trunc), 0);
  ck_assert_int_eq(xml_attr(doc, end, "zz", out, sizeof out), -1);
  ck_assert_int_eq(xml_tag_attr(doc, end, "a", out, sizeof out), 0);
  ck_assert_str_eq(out, "1");
  ck_assert_int_eq(xml_tag_attr(doc + 1, end, "a", out, sizeof out), -1);
  ck_assert_int_eq(xml_tag_attr("</x>", "</x>" + 4, "a", out, sizeof out), -1);
  ck_assert_int_eq(xml_tag_attr_chk(doc, end, "a", out, 1, &trunc), 0);
  ck_assert_int_eq(xml_attr_list(" a='1' b=\"2\"", " a='1' b=\"2\"" + 12, "b", out, sizeof out), 0);
  ck_assert_str_eq(out, "2");
}
END_TEST

static Suite *xml_util_suite(void) {
  Suite *s = suite_create("xml_util");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xml_escape_escapes_all_five_entities);
  tcase_add_test(tc, xml_escape_drops_chars_invalid_in_xml_10);
  tcase_add_test(tc, xml_escape_replaces_invalid_utf8);
  tcase_add_test(tc, xml_elem_text_rejects_control_char_refs);
  tcase_add_test(tc, xml_to_utf8_converts_declared_latin1);
  tcase_add_test(tc, xml_to_utf8_converts_windows_1252);
  tcase_add_test(tc, xml_to_utf8_leaves_utf8_and_undeclared_alone);
  tcase_add_test(tc, xml_to_utf8_rejects_unknown_encoding);
  tcase_add_test(tc, xml_to_utf8_grows_its_buffer_and_survives_allocation_failure);
  tcase_add_test(tc, xml_to_utf8_reads_declarations_strictly);
  tcase_add_test(tc, xml_scan_handles_odd_markup);
  tcase_add_test(tc, xml_escape_covers_every_character_class);
  tcase_add_test(tc, xml_sink_decodes_entities_and_cuts_on_sequence_boundaries);
  tcase_add_test(tc, xml_query_api_covers_each_path);
  tcase_add_test(tc, xml_attr_extracts_and_decodes_quoted_value);
  tcase_add_test(tc, xml_attr_returns_error_when_missing);
  tcase_add_test(tc, xml_elem_text_extracts_and_decodes_body);
  tcase_add_test(tc, xml_elem_text_decodes_numeric_char_refs);
  tcase_add_test(tc, xml_elem_text_rejects_self_closing_tag);
  tcase_add_test(tc, xml_elem_text_does_not_match_prefix_tag_name);
  tcase_add_test(tc, xml_elem_text_truncation_keeps_utf8_whole);
  tcase_add_test(tc, xml_elem_text_chk_reports_truncation);
  tcase_add_test(tc, xml_elem_text_dup_returns_full_decoded_text);
  tcase_add_test(tc, xml_elem_text_dup_keeps_text_past_1k);
  tcase_add_test(tc, xml_attr_chk_reports_truncation);
  tcase_add_test(tc, xml_attr_accepts_single_quotes_and_spaced_equals);
  tcase_add_test(tc, xml_attr_ignores_longer_names_text_and_comments);
  tcase_add_test(tc, xml_tag_attr_looks_only_at_the_start_tag);
  tcase_add_test(tc, xml_attr_list_reads_bare_attribute_text);
  tcase_add_test(tc, xml_elem_text_unwraps_cdata);
  tcase_add_test(tc, xml_elem_text_matches_namespace_prefix);
  tcase_add_test(tc, xml_elem_text_skips_comments_and_child_markup);
  tcase_add_test(tc, xml_elem_text_handles_same_name_nesting);
  tcase_add_test(tc, xml_numeric_ref_rejects_invalid_code_points);
  tcase_add_test(tc, for_each_xml_elem_visits_elements_by_local_name);
  tcase_add_test(tc, for_each_xml_elem_ignores_unclosed_trailing_element);
  tcase_add_test(tc, for_each_xml_elem_chk_reports_unclosed_element);
  tcase_add_test(tc, for_each_xml_elem_chk_leaves_unclosed_alone_when_balanced);
  tcase_add_test(tc, xml_find_start_needs_no_close_tag);
  tcase_add_test(tc, xml_doc_resolves_namespaces_and_decodes);
  tcase_add_test(tc, xml_doc_keeps_mixed_text);
  tcase_add_test(tc, xml_doc_normalizes_line_ends_and_attr_space);
  tcase_add_test(tc, xml_doc_rejects_malformed);
  tcase_add_test(tc, xml_doc_rejects_more_malformed_forms);
  tcase_add_test(tc, xml_doc_accepts_every_markup_form);
  tcase_add_test(tc, xml_doc_grows_its_name_table_and_limits_depth);
  tcase_add_test(tc, xml_doc_parse_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(xml_util_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
