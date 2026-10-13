/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/core/time_ep.h"

START_TEST(render_iso_ms_utc) {
  char buf[TIME_EP_BODY_MAX];
  size_t len = time_ep_render(buf, sizeof buf);
  ck_assert_uint_eq(len, 24);
  ck_assert_int_eq(buf[4], '-');
  ck_assert_int_eq(buf[10], 'T');
  ck_assert_int_eq(buf[19], '.');
  ck_assert_int_eq(buf[23], 'Z');
  ck_assert_uint_eq(strlen(buf), len);
}
END_TEST

START_TEST(render_small_buf_fails) {
  char buf[8];
  ck_assert_uint_eq(time_ep_render(buf, sizeof buf), 0);
}
END_TEST

START_TEST(url_cfg_verbatim) {
  char out[TIME_EP_URL_MAX];
  time_ep_utc_url(out, sizeof out, "http://time.example/utc", 1, "h:1");
  ck_assert_str_eq(out, "http://time.example/utc");
}
END_TEST

START_TEST(url_from_host_scheme) {
  char out[TIME_EP_URL_MAX];
  time_ep_utc_url(out, sizeof out, "", 1, "tv.example:9443");
  ck_assert_str_eq(out, "https://tv.example:9443/time");
  time_ep_utc_url(out, sizeof out, NULL, 0, "[::1]:9080");
  ck_assert_str_eq(out, "http://[::1]:9080/time");
}
END_TEST

START_TEST(url_bad_host_falls_back) {
  char out[TIME_EP_URL_MAX];
  const char *bad[] = {NULL, "", "a b", "a/b", "a\"b", "a<b>"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    time_ep_utc_url(out, sizeof out, "", 1, bad[i]);
    ck_assert_str_eq(out, "https://time.akamai.com/?iso&ms");
  }
}
END_TEST

static Suite *time_ep_suite(void) {
  Suite *s = suite_create("time_ep");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, render_iso_ms_utc);
  tcase_add_test(tc, render_small_buf_fails);
  tcase_add_test(tc, url_cfg_verbatim);
  tcase_add_test(tc, url_from_host_scheme);
  tcase_add_test(tc, url_bad_host_falls_back);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(time_ep_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
