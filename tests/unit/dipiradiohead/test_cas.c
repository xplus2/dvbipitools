/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/cas/cas.h"

START_TEST(ninetyk_to_ms_exact_multiple) {
  uint64_t rem = 0;
  ck_assert_uint_eq(cas_90k_to_ms(90000, &rem), 1000u);
  ck_assert_uint_eq(rem, 0u);
}
END_TEST

START_TEST(ninetyk_to_ms_carries_remainder) {
  uint64_t rem = 0;
  ck_assert_uint_eq(cas_90k_to_ms(2090, &rem), 23u); /* 2090/90 = 23 r 20 */
  ck_assert_uint_eq(rem, 20u);
}
END_TEST

START_TEST(ninetyk_to_ms_no_drift_over_many_calls) {
  uint64_t rem = 0;
  unsigned long total_ms = 0;
  int i;
  /* 90x989 ticks = 89010 = 989ms exactly; drift would show up as leftover rem */
  for (i = 0; i < 90; i++)
    total_ms += cas_90k_to_ms(989, &rem);
  ck_assert_uint_eq(total_ms, 989u);
  ck_assert_uint_eq(rem, 0u);
}
END_TEST

START_TEST(ninetyk_to_ms_zero_delta) {
  uint64_t rem = 42;
  ck_assert_uint_eq(cas_90k_to_ms(0, &rem), 0u);
  ck_assert_uint_eq(rem, 42u);
}
END_TEST

START_TEST(cas_start_rejects_zero_audio_pids) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_ptr_null(cas_start(&cfg, NULL, 0));
}
END_TEST

START_TEST(cas_start_rejects_too_many_audio_pids) {
  unsigned pids[64];
  config_t cfg;
  size_t i;
  memset(&cfg, 0, sizeof cfg);
  for (i = 0; i < 64; i++)
    pids[i] = 0x0100 + (unsigned)i;
  ck_assert_ptr_null(cas_start(&cfg, pids, 64));
}
END_TEST

static Suite *cas_suite(void) {
  Suite *s = suite_create("cas");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ninetyk_to_ms_exact_multiple);
  tcase_add_test(tc, ninetyk_to_ms_carries_remainder);
  tcase_add_test(tc, ninetyk_to_ms_no_drift_over_many_calls);
  tcase_add_test(tc, ninetyk_to_ms_zero_delta);
  tcase_add_test(tc, cas_start_rejects_zero_audio_pids);
  tcase_add_test(tc, cas_start_rejects_too_many_audio_pids);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cas_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
