/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/xml_util.h"

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

START_TEST(xml_find_start_needs_no_close_tag) {
  static const char doc[] = "<?xml version='1.0'?><!DOCTYPE a [<!ENTITY e \">\">]><sd:Root a='1'>";
  ck_assert_ptr_nonnull(xml_find_start(doc, doc + strlen(doc), "Root"));
  ck_assert_ptr_null(xml_find_start(doc, doc + strlen(doc), "Ro"));
}
END_TEST

static Suite *xml_util_suite(void) {
  Suite *s = suite_create("xml_util");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xml_escape_escapes_all_five_entities);
  tcase_add_test(tc, xml_attr_extracts_and_decodes_quoted_value);
  tcase_add_test(tc, xml_attr_returns_error_when_missing);
  tcase_add_test(tc, xml_elem_text_extracts_and_decodes_body);
  tcase_add_test(tc, xml_elem_text_decodes_numeric_char_refs);
  tcase_add_test(tc, xml_elem_text_rejects_self_closing_tag);
  tcase_add_test(tc, xml_elem_text_does_not_match_prefix_tag_name);
  tcase_add_test(tc, xml_elem_text_truncation_keeps_utf8_whole);
  tcase_add_test(tc, xml_elem_text_chk_reports_truncation);
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
  tcase_add_test(tc, xml_find_start_needs_no_close_tag);
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
