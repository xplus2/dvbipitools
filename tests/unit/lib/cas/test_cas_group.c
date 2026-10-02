/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_group.h"

START_TEST(fallback_no_vendors_is_active) {
  ck_assert_int_eq(cas_group_fallback_active_calc(0, NULL, NULL), 1);
}
END_TEST

START_TEST(fallback_all_alive_none_required_is_inactive) {
  int required[3] = {0, 0, 0};
  int alive[3] = {1, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_one_alive_none_required_is_inactive) {
  int required[3] = {0, 0, 0};
  int alive[3] = {0, 1, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_zero_alive_none_required_is_active) {
  int required[3] = {0, 0, 0};
  int alive[3] = {0, 0, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 1);
}
END_TEST

START_TEST(fallback_required_down_others_alive_is_active) {
  int required[3] = {1, 0, 0};
  int alive[3] = {0, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 1);
}
END_TEST

START_TEST(fallback_required_alive_others_down_is_inactive) {
  int required[3] = {1, 0, 0};
  int alive[3] = {1, 0, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_all_required_all_alive_is_inactive) {
  int required[3] = {1, 1, 1};
  int alive[3] = {1, 1, 1};
  ck_assert_int_eq(cas_group_fallback_active_calc(3, required, alive), 0);
}
END_TEST

START_TEST(fallback_one_of_two_required_down_is_active) {
  int required[2] = {1, 1};
  int alive[2] = {1, 0};
  ck_assert_int_eq(cas_group_fallback_active_calc(2, required, alive), 1);
}
END_TEST

START_TEST(csa1_checksum_matches_known_values) {
  /* k0,k1,k2 = 0x01,0x02,0x03 -> checksum 0x06; k4,k5,k6 = 0x10,0x20,0x30 -> checksum 0x60 */
  unsigned char cw[8] = {0x01, 0x02, 0x03, 0xAA, 0x10, 0x20, 0x30, 0xAA};
  csa1_apply_cw_checksum(cw);
  ck_assert_uint_eq(cw[3], 0x06);
  ck_assert_uint_eq(cw[7], 0x60);
  /* free bytes untouched */
  ck_assert_uint_eq(cw[0], 0x01);
  ck_assert_uint_eq(cw[1], 0x02);
  ck_assert_uint_eq(cw[2], 0x03);
  ck_assert_uint_eq(cw[4], 0x10);
  ck_assert_uint_eq(cw[5], 0x20);
  ck_assert_uint_eq(cw[6], 0x30);
}
END_TEST

START_TEST(csa1_checksum_wraps_modulo_256) {
  unsigned char cw[8] = {0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x00};
  csa1_apply_cw_checksum(cw);
  ck_assert_uint_eq(cw[3], (unsigned char)((0xFF + 0xFF + 0xFF) % 256));
  ck_assert_uint_eq(cw[7], (unsigned char)((0xFF + 0xFF + 0xFF) % 256));
}
END_TEST

static cas_group_cfg_t valid_cfg(void) {
  cas_group_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.algo = SCRAMBLE_ALGO_CSA2;
  cfg.cp_duration_ms = 10000;
  cfg.pids[0] = 0x100;
  cfg.pids[1] = 0x101;
  cfg.pid_count = 2;
  cfg.vendor_count = 2;
  cfg.vendors[0].super_cas_id = 0x4A750001;
  cfg.vendors[0].ecm_pid = 0x1FF0;
  cfg.vendors[0].emm_pid = 0x1FF1;
  cfg.vendors[1].super_cas_id = 0x09630002;
  cfg.vendors[1].ecm_pid = 0x1FF2;
  cfg.vendors[1].emm_pid = 0x1FF3;
  return cfg;
}

typedef struct {
  const char *name;
  size_t vendor_count;
  size_t pid_count;
  unsigned cp_duration_ms;
} bad_cfg_case_t;

static const bad_cfg_case_t bad_cfg_cases[] = {
    {"one vendor more than slots", CAS_GROUP_MAX_VENDORS + 1, 2, 10000},
    {"huge vendor count", (size_t)-1, 2, 10000},
    {"one pid more than slots", 2, CAS_CORE_MAX_PIDS + 1, 10000},
    {"huge pid count", 2, (size_t)-1, 10000},
    {"zero crypto period", 2, 2, 0},
};

START_TEST(start_rejects_out_of_range_configuration) {
  const bad_cfg_case_t *c = &bad_cfg_cases[_i];
  cas_group_cfg_t cfg = valid_cfg();
  cas_group_t *g;

  cfg.vendor_count = c->vendor_count;
  cfg.pid_count = c->pid_count;
  cfg.cp_duration_ms = c->cp_duration_ms;
  g = cas_group_start(&cfg, 0x100);
  ck_assert_msg(g == NULL, "%s: accepted", c->name);
}
END_TEST

START_TEST(start_rejects_null_configuration) {
  ck_assert_ptr_null(cas_group_start(NULL, 0x100));
}
END_TEST

START_TEST(start_accepts_boundary_configuration) {
  cas_group_cfg_t cfg = valid_cfg();
  cas_group_t *g;

  cfg.vendor_count = CAS_GROUP_MAX_VENDORS;
  cfg.pid_count = CAS_CORE_MAX_PIDS;
  cfg.cp_duration_ms = 1;
  g = cas_group_start(&cfg, 0x100);
  ck_assert_ptr_nonnull(g);
  ck_assert_uint_eq(cas_group_vendor_count(g), (size_t)CAS_GROUP_MAX_VENDORS);
  cas_group_stop(g);
}
END_TEST

START_TEST(descriptor_builders_reject_every_short_buffer) {
  cas_group_cfg_t cfg = valid_cfg();
  cas_group_t *g = cas_group_start(&cfg, 0x100);
  unsigned char out[256];
  size_t prog_len;
  size_t cat_len;

  ck_assert_ptr_nonnull(g);
  ck_assert_int_eq(cas_group_failed(g), 0);
  ck_assert_uint_eq(cas_group_vendor_ecm_pid(g, 1), 0x1FF2u);
  ck_assert_uint_eq(cas_group_vendor_emm_pid(g, 1), 0x1FF3u);
  ck_assert_uint_eq(cas_group_vendor_super_cas_id(g, 0), 0x4A750001u);

  prog_len = cas_group_prog_desc(g, out, sizeof out);
  ck_assert_uint_gt(prog_len, 0u);
  for (size_t cap = 0; cap < prog_len; cap++) ck_assert_uint_eq(cas_group_prog_desc(g, out, cap), 0u);
  ck_assert_uint_eq(cas_group_prog_desc(g, out, prog_len), prog_len);

  cat_len = cas_group_build_cat(g, out, sizeof out);
  ck_assert_uint_gt(cat_len, 0u);
  ck_assert_uint_eq(out[0], 0x01);
  for (size_t cap = 0; cap < cat_len; cap++) ck_assert_uint_eq(cas_group_build_cat(g, out, cap), 0u);
  ck_assert_uint_eq(cas_group_build_cat(g, out, cat_len), cat_len);
  cas_group_stop(g);
}
END_TEST

START_TEST(no_vendor_group_builds_only_the_scrambling_descriptor) {
  cas_group_cfg_t cfg = valid_cfg();
  cas_group_t *g;
  unsigned char out[64];
  size_t n;

  cfg.vendor_count = 0;
  g = cas_group_start(&cfg, 0x100);
  ck_assert_ptr_nonnull(g);
  n = cas_group_prog_desc(g, out, sizeof out);
  ck_assert_uint_gt(n, 0u);
  ck_assert_uint_ne(out[0], 0x09);
  cas_group_stop(g);
}
END_TEST

static Suite *cas_group_suite(void) {
  Suite *s = suite_create("cas_group");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fallback_no_vendors_is_active);
  tcase_add_test(tc, fallback_all_alive_none_required_is_inactive);
  tcase_add_test(tc, fallback_one_alive_none_required_is_inactive);
  tcase_add_test(tc, fallback_zero_alive_none_required_is_active);
  tcase_add_test(tc, fallback_required_down_others_alive_is_active);
  tcase_add_test(tc, fallback_required_alive_others_down_is_inactive);
  tcase_add_test(tc, fallback_all_required_all_alive_is_inactive);
  tcase_add_test(tc, fallback_one_of_two_required_down_is_active);
  tcase_add_test(tc, csa1_checksum_matches_known_values);
  tcase_add_test(tc, csa1_checksum_wraps_modulo_256);
  tcase_add_loop_test(tc, start_rejects_out_of_range_configuration, 0, (int)(sizeof bad_cfg_cases / sizeof bad_cfg_cases[0]));
  tcase_add_test(tc, start_rejects_null_configuration);
  tcase_add_test(tc, start_accepts_boundary_configuration);
  tcase_add_test(tc, descriptor_builders_reject_every_short_buffer);
  tcase_add_test(tc, no_vendor_group_builds_only_the_scrambling_descriptor);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cas_group_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
