/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/xml_util.h"

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
