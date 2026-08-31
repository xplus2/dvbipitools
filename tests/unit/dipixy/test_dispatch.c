/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/args.h"
#include "dipixy/reactor/internal.h"

static struct phr_header make_header(const char *name, const char *value) {
  struct phr_header h;
  h.name = name;
  h.name_len = strlen(name);
  h.value = value;
  h.value_len = strlen(value);
  return h;
}

START_TEST(find_header_matches_case_insensitively) {
  struct phr_header hdrs[2];
  char out[64];
  hdrs[0] = make_header("Content-Type", "text/plain");
  hdrs[1] = make_header("X-Foo", "bar");

  ck_assert_int_eq(find_header(hdrs, 2, "content-type", out, sizeof out), 1);
  ck_assert_str_eq(out, "text/plain");
  ck_assert_int_eq(find_header(hdrs, 2, "X-FOO", out, sizeof out), 1);
  ck_assert_str_eq(out, "bar");
}
END_TEST

START_TEST(find_header_missing_returns_zero) {
  struct phr_header hdrs[1];
  char out[64];
  hdrs[0] = make_header("X-Foo", "bar");
  ck_assert_int_eq(find_header(hdrs, 1, "Authorization", out, sizeof out), 0);
}
END_TEST

START_TEST(find_header_truncates_to_buffer) {
  struct phr_header hdrs[1];
  char out[4];
  hdrs[0] = make_header("X-Foo", "abcdef");
  ck_assert_int_eq(find_header(hdrs, 1, "X-Foo", out, sizeof out), 1);
  ck_assert_str_eq(out, "abc"); /* outsz=4: 3 chars + NUL */
}
END_TEST

START_TEST(find_header_empty_list_returns_zero) {
  char out[8];
  ck_assert_int_eq(find_header(NULL, 0, "X-Foo", out, sizeof out), 0);
}
END_TEST

START_TEST(strip_etag_quotes_removes_bare_quotes) {
  char v[32] = "\"abc123\"";
  strip_etag_quotes(v);
  ck_assert_str_eq(v, "abc123");
}
END_TEST

START_TEST(strip_etag_quotes_removes_weak_prefix_and_quotes) {
  char v[32] = "W/\"abc123\"";
  strip_etag_quotes(v);
  ck_assert_str_eq(v, "abc123");
}
END_TEST

START_TEST(strip_etag_quotes_leaves_unquoted_alone) {
  char v[32] = "abc123";
  strip_etag_quotes(v);
  ck_assert_str_eq(v, "abc123");
}
END_TEST

START_TEST(auth_disabled_when_cfg_empty) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(http_auth_ok(&cfg, NULL), 1);
  ck_assert_int_eq(http_auth_ok(&cfg, "Basic anything"), 1);
}
END_TEST

START_TEST(auth_requires_exact_match_when_enabled) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  strcpy(cfg.http_auth, "Basic dXNlcjpwYXNz");
  ck_assert_int_eq(http_auth_ok(&cfg, NULL), 0);
  ck_assert_int_eq(http_auth_ok(&cfg, "Basic wrong"), 0);
  ck_assert_int_eq(http_auth_ok(&cfg, "Basic dXNlcjpwYXNz"), 1);
}
END_TEST

START_TEST(cors_defaults_to_star_when_unset) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = NULL;
  ck_assert_str_eq(cors_match(&cfg, "https://example.com", &vary), "*");
  ck_assert_int_eq(vary, 0);
}
END_TEST

START_TEST(cors_star_entry_in_list_matches_anything) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com, *, https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://whatever.example", &vary), "*");
  ck_assert_int_eq(vary, 0);
}
END_TEST

START_TEST(cors_allowlist_matches_and_sets_vary) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com,https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://b.com", &vary), "https://b.com");
  ck_assert_int_eq(vary, 1);
}
END_TEST

START_TEST(cors_allowlist_trims_surrounding_spaces) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com , https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://b.com", &vary), "https://b.com");
  ck_assert_int_eq(vary, 1);
}
END_TEST

START_TEST(cors_allowlist_rejects_unlisted_origin) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com,https://b.com";
  ck_assert_ptr_null(cors_match(&cfg, "https://evil.example", &vary));
  ck_assert_int_eq(vary, 0);
}
END_TEST

static Suite *dispatch_suite(void) {
  Suite *s = suite_create("dipixy_dispatch");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, find_header_matches_case_insensitively);
  tcase_add_test(tc, find_header_missing_returns_zero);
  tcase_add_test(tc, find_header_truncates_to_buffer);
  tcase_add_test(tc, find_header_empty_list_returns_zero);
  tcase_add_test(tc, strip_etag_quotes_removes_bare_quotes);
  tcase_add_test(tc, strip_etag_quotes_removes_weak_prefix_and_quotes);
  tcase_add_test(tc, strip_etag_quotes_leaves_unquoted_alone);
  tcase_add_test(tc, auth_disabled_when_cfg_empty);
  tcase_add_test(tc, auth_requires_exact_match_when_enabled);
  tcase_add_test(tc, cors_defaults_to_star_when_unset);
  tcase_add_test(tc, cors_star_entry_in_list_matches_anything);
  tcase_add_test(tc, cors_allowlist_matches_and_sets_vary);
  tcase_add_test(tc, cors_allowlist_trims_surrounding_spaces);
  tcase_add_test(tc, cors_allowlist_rejects_unlisted_origin);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dispatch_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
