/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/core/status.h"
#include "dipixy/reactor/reactor_tls.h"

static uint64_t g_in_bytes;
static uint64_t g_out_bytes;
static double g_now;

uint64_t capture_bytes_total(void) { return g_in_bytes; }
unsigned long long reactor_bytes_served_total(void) { return g_out_bytes; }
double mono_seconds(void) { return g_now; }
int reactor_worker_count(void) { return 2; }
int channels_refresh_active(void) { return 0; }
int tls_is_running(void) { return 0; }

int tls_cert_detail(const char *path, int from_file, tls_cert_detail_t *out) {
  (void)path;
  (void)from_file;
  memset(out, 0, sizeof *out);
  return 0;
}

static config_t g_cfg;

static int field(const char *key, char *out, size_t cap) {
  char *json;
  size_t len;
  char pat[64];
  const char *p;
  size_t n;

  ck_assert_int_eq(dipixy_status_render_json(&g_cfg, &json, &len), 0);
  snprintf(pat, sizeof pat, "\"%s\":", key);
  p = strstr(json, pat);
  if (!p) return -1;
  p += strlen(pat);
  n = strcspn(p, ",}");
  ck_assert_uint_lt(n, cap);
  memcpy(out, p, n);
  out[n] = '\0';
  return 0;
}

static void expect_rates(const char *in, const char *out) {
  char v[32];

  ck_assert_int_eq(field("in_mbps", v, sizeof v), 0);
  ck_assert_str_eq(v, in);
  ck_assert_int_eq(field("out_mbps", v, sizeof v), 0);
  ck_assert_str_eq(v, out);
}

static void tick_at(double t, uint64_t in_bytes, uint64_t out_bytes) {
  g_now = t;
  g_in_bytes = in_bytes;
  g_out_bytes = out_bytes;
  dipixy_status_tick();
}

typedef struct {
  double dt;
  uint64_t din;
  uint64_t dout;
  const char *in;
  const char *out;
} rate_case_t;

static const rate_case_t rate_cases[] = {
  {1.0, 125000, 250000, "1.000", "2.000"},
  {1.0, 125000, 0, "1.000", "0.000"},
  {1.0, 0, 125000, "0.000", "1.000"},
  {1.0, 0, 0, "0.000", "0.000"},
  {2.0, 250000, 500000, "1.000", "2.000"},
  {0.5, 62500, 125000, "1.000", "2.000"},
  {1.0, 1250, 2500, "0.010", "0.020"},
  {1.0, 62, 62, "0.000", "0.000"},
  {1.0, 63, 63, "0.001", "0.001"},
  {1.0, 1000000000ull, 500000000ull, "8000.000", "4000.000"},
  {1.0, 17179869, 1, "137.439", "0.000"},
};

START_TEST(rates_follow_byte_deltas_over_elapsed_time) {
  const rate_case_t *rc = &rate_cases[_i];

  tick_at(100.0, 1000, 5000);
  tick_at(100.0 + rc->dt, 1000 + rc->din, 5000 + rc->dout);
  expect_rates(rc->in, rc->out);
}
END_TEST

START_TEST(first_tick_reports_zero_rates) {
  expect_rates("0.000", "0.000");
  tick_at(50.0, 123456789, 987654321);
  expect_rates("0.000", "0.000");
}
END_TEST

START_TEST(rate_falls_back_to_zero_when_traffic_stops) {
  tick_at(10.0, 0, 0);
  tick_at(11.0, 125000, 125000);
  expect_rates("1.000", "1.000");
  tick_at(12.0, 125000, 125000);
  expect_rates("0.000", "0.000");
}
END_TEST

START_TEST(counter_going_backwards_counts_as_zero) {
  tick_at(10.0, 500000, 500000);
  tick_at(11.0, 1000, 500000 + 125000);
  expect_rates("0.000", "1.000");
  tick_at(12.0, 1000 + 125000, 10);
  expect_rates("1.000", "0.000");
}
END_TEST

START_TEST(tick_without_elapsed_time_keeps_previous_rates) {
  tick_at(10.0, 0, 0);
  tick_at(11.0, 125000, 250000);
  tick_at(11.0, 999999, 999999);
  expect_rates("1.000", "2.000");
  tick_at(11.0 - 0.5, 1999999, 1999999);
  expect_rates("1.000", "2.000");
}
END_TEST

START_TEST(rate_uses_deltas_since_the_previous_tick_only) {
  tick_at(10.0, 0, 0);
  tick_at(11.0, 125000, 125000);
  tick_at(13.0, 125000 + 250000, 125000 + 500000);
  expect_rates("1.000", "2.000");
}
END_TEST

START_TEST(rss_is_a_whole_number_of_pages_within_the_address_space) {
  char v[32];
  unsigned long long rss;
  long page = sysconf(_SC_PAGESIZE);
  unsigned long total_pages = 0;
  unsigned long rss_pages = 0;
  FILE *f = fopen("/proc/self/statm", "r");

  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(fscanf(f, "%lu %lu", &total_pages, &rss_pages), 2);
  fclose(f);
  ck_assert_int_eq(field("rss_bytes", v, sizeof v), 0);
  rss = strtoull(v, NULL, 10);
  ck_assert_uint_gt(rss, 0);
  ck_assert_uint_eq(rss % (unsigned long long)page, 0);
  ck_assert_uint_le(rss, (unsigned long long)total_pages * (unsigned long long)page);
}
END_TEST

static void setup(void) {
  memset(&g_cfg, 0, sizeof g_cfg);
  g_in_bytes = 0;
  g_out_bytes = 0;
  g_now = 0.0;
}

static Suite *status_suite(void) {
  Suite *s = suite_create("dipixy_status");
  TCase *tc = tcase_create("core");

  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_loop_test(tc, rates_follow_byte_deltas_over_elapsed_time, 0, (int)(sizeof rate_cases / sizeof rate_cases[0]));
  tcase_add_test(tc, first_tick_reports_zero_rates);
  tcase_add_test(tc, rate_falls_back_to_zero_when_traffic_stops);
  tcase_add_test(tc, counter_going_backwards_counts_as_zero);
  tcase_add_test(tc, tick_without_elapsed_time_keeps_previous_rates);
  tcase_add_test(tc, rate_uses_deltas_since_the_previous_tick_only);
  tcase_add_test(tc, rss_is_a_whole_number_of_pages_within_the_address_space);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(status_suite());
  int failed;

  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
