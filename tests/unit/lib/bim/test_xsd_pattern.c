/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"

typedef struct {
  const char *re;
  const char *v;
  int want;
} pcase_t;

static const pcase_t matches[] = {
  {"abc", "abc", 1}, {"abc", "abd", 0}, {"a|b|c", "b", 1}, {"a|b|c", "d", 0}, {"", "", 1}, {"", "x", 0}, {"a?", "", 1}, {"a?", "a", 1}, {"a?", "aa", 0}, {"a*", "aaaa", 1}, {"a+", "", 0},
  {"a+", "aaa", 1}, {"a{3}", "aaa", 1}, {"a{3}", "aa", 0}, {"a{2,}", "aaaa", 1}, {"a{2,}", "a", 0}, {"a{1,3}", "aaa", 1}, {"a{1,3}", "aaaa", 0}, {"(ab)+", "ababab", 1}, {"(ab)+", "aba", 0},
  {"(a|b)*c", "abbac", 1}, {"((a|b)c)*", "acbc", 1}, {"(a*)*", "aaa", 1}, {"(a?)+b", "b", 0}, {"(a?)+b", "ab", 1}, {".", "x", 1}, {".", "\n", 0}, {".", "\r", 0}, {"..", "\xC3\xA9z", 1}, {".", "\xC3\xA9", 1},
  {"\\n", "\n", 1}, {"\\r", "\r", 1}, {"\\t", "\t", 1}, {"\\\\", "\\", 1}, {"\\|", "|", 1}, {"\\.", ".", 1}, {"\\-", "-", 1}, {"\\^", "^", 1}, {"\\?", "?", 1}, {"\\*", "*", 1}, {"\\+", "+", 1},
  {"\\{", "{", 1}, {"\\}", "}", 1}, {"\\(", "(", 1}, {"\\)", ")", 1}, {"\\[", "[", 1}, {"\\]", "]", 1}, {"\\d+", "123", 1}, {"\\d", "a", 0}, {"\\d", "\xD9\xA0", 1}, {"\\D", "a", 1}, {"\\D", "5", 0},
  {"\\s+", " \t\r\n", 1}, {"\\S", " ", 0}, {"\\S+", "ab", 1}, {"\\i\\c*", "a-b.c1", 1}, {"\\i", "1", 0}, {"\\I", "1", 1}, {"\\c", "-", 1}, {"\\C", "a", 0}, {"\\C", " ", 1}, {"\\i", "\xC3\xA9", 1},
  {"[abc]", "b", 1}, {"[abc]", "d", 0}, {"[a-c]+", "abcba", 1}, {"[a-c]", "d", 0}, {"[^abc]", "d", 1}, {"[^abc]", "a", 0}, {"[a\\-c]", "-", 1}, {"[\\n\\t]", "\t", 1}, {"[\\\\]", "\\", 1},
  {"[\\d]", "5", 1}, {"[\\D]", "a", 1}, {"[\\s\\d]+", "1 2", 1}, {"[a-z-[aeiou]]+", "xyz", 1}, {"[a-z-[aeiou]]", "e", 0}, {"[a-z-[a-f-[a-c]]]", "d", 0}, {"[a-z-[a-f-[a-c]]]", "b", 1},
  {"[\\x]", "x", -1}, {"[a-\\n]", "a", -1}, {"[-a]", "-", 1}, {"[a-]", "-", 1}, {"[\xC3\xA0-\xC3\xBF]", "\xC3\xA9", 1}, {"[Z-\\]]", "\\", 1}, {"[\\]]", "]", 1},
  {"\xC3\xA9+", "\xC3\xA9\xC3\xA9", 1}, {"x{0}", "", 1}, {"x{0,0}y", "y", 1},
};

static const char *const invalid[] = {
  "(", ")", "a)", "(a", "[", "[a", "[]", "[^]", "[a-", "[b-a]", "[[a]]", "[a-z-[]]", "[a-z-[b]", "\\", "\\p{L}", "\\w", "\\x", "a{", "a{x}", "a{2,1}", "a{2", "a{2,", "a{100000}", "a{1,2",
  "a**", "*", "+", "?", "{1}", "a||b)", "\xC3", "\xFF", "[\xC3]", "a\xE2\x82", "[a-\xC3]", "((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((((a))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))",
};

static xsd_pattern_t *compile(xsd_set_t *s, const char *re) { return xsd_pattern_compile(s, re); }

START_TEST(pattern_matches_the_supported_syntax) {
  for (size_t i = 0; i < sizeof matches / sizeof *matches; i++) {
    xsd_set_t *s = xsd_set_new();
    xsd_pattern_t *p = compile(s, matches[i].re);
    if (matches[i].want == -1) {
      ck_assert_msg(!p, "pattern %s compiled", matches[i].re);
    } else {
      ck_assert_msg(p != NULL, "pattern %s rejected", matches[i].re);
      ck_assert_msg(xsd_pattern_match(p, matches[i].v) == matches[i].want, "pattern %s value %s", matches[i].re, matches[i].v);
    }
    xsd_set_free(s);
  }
}
END_TEST

START_TEST(pattern_rejects_malformed_syntax) {
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
    xsd_set_t *s = xsd_set_new();
    ck_assert_msg(compile(s, invalid[i]) == NULL, "pattern %s compiled", invalid[i]);
    xsd_set_free(s);
  }
}
END_TEST

START_TEST(pattern_match_reports_runaway_backtracking) {
  xsd_set_t *s = xsd_set_new();
  xsd_pattern_t *p = compile(s, "(a|a)*(a|a)*(a|a)*b");
  char text[64];
  memset(text, 'a', sizeof text - 1);
  text[sizeof text - 1] = 0;
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(xsd_pattern_match(p, text), -1);
  xsd_set_free(s);
}
END_TEST

START_TEST(pattern_malformed_utf8_values_do_not_match) {
  xsd_set_t *s = xsd_set_new();
  xsd_pattern_t *p = compile(s, ".*");
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(xsd_pattern_match(p, "\xC3"), 0);
  ck_assert_int_eq(xsd_pattern_match(p, "a\xFFz"), 0);
  ck_assert_int_eq(xsd_pattern_match(p, "\xE2\x82"), 0);
  ck_assert_int_eq(xsd_pattern_match(p, "\xC3\x28"), 0);
  ck_assert_int_eq(xsd_pattern_match(p, "\xF0\x9F\x98\x80"), 1);
  xsd_set_free(s);
}
END_TEST

START_TEST(pattern_compile_allocation_failures) {
  static const char re[] = "(a|b[c-f-[de]])+\\d{2,4}(x*|y?)\\i\\c*";
  for (long n = 0;; n++) {
    xsd_set_t *s = xsd_set_new();
    xsd_pattern_t *p;
    int hit;
    alloc_fail_at(n);
    p = xsd_pattern_compile(s, re);
    hit = alloc_fail_end();
    if (!hit) ck_assert_ptr_nonnull(p);
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

static Suite *pattern_suite(void) {
  Suite *s = suite_create("bim_xsd_pattern");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pattern_matches_the_supported_syntax);
  tcase_add_test(tc, pattern_rejects_malformed_syntax);
  tcase_add_test(tc, pattern_match_reports_runaway_backtracking);
  tcase_add_test(tc, pattern_malformed_utf8_values_do_not_match);
  tcase_add_test(tc, pattern_compile_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pattern_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
