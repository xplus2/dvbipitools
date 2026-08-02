/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/cas/cas.h"

static void build_pcr_packet(unsigned char pkt[188], int with_pcr, uint64_t base, unsigned ext) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x40;
  if (!with_pcr) {
    pkt[3] = 0x10; /* payload only, no adaptation field */
    return;
  }
  pkt[3] = 0x30; /* adaptation field + payload */
  pkt[4] = 183;  /* adaptation_field_length: room for flags+PCR+stuffing */
  pkt[5] = 0x10; /* PCR_flag */
  pkt[6] = (unsigned char)(base >> 25);
  pkt[7] = (unsigned char)(base >> 17);
  pkt[8] = (unsigned char)(base >> 9);
  pkt[9] = (unsigned char)(base >> 1);
  pkt[10] = (unsigned char)(((base & 1) << 7) | 0x7E | ((ext >> 8) & 1));
  pkt[11] = (unsigned char)ext;
}

START_TEST(parse_pcr_recovers_known_value) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 12345678ULL, 150);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 1);
  ck_assert_uint_eq(pcr27, 12345678ULL * 300 + 150);
}
END_TEST

START_TEST(parse_pcr_zero_extension) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 1, 0);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 1);
  ck_assert_uint_eq(pcr27, 300ULL);
}
END_TEST

START_TEST(parse_pcr_no_adaptation_field) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 0, 0, 0);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(parse_pcr_flag_not_set) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 999, 1);
  pkt[5] = 0x00; /* clear PCR_flag */
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(parse_pcr_adaptation_field_too_short) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 999, 1);
  pkt[4] = 1; /* only the flags byte, no room for the 6-byte PCR field */
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(pcr_plausible_normal_delta_accepted) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10; /* 10s */
  uint64_t new27 = 27000000ULL * 11;  /* 11s: 1s later */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 1);
  ck_assert_double_eq_tol(delta_s, 1.0, 0.01);
}
END_TEST

START_TEST(pcr_plausible_wraparound_accepted) {
  double delta_s;
  uint64_t last27 = CAS_PCR_MODULUS - 27000000ULL; /* 1s before wrap */
  uint64_t new27 = 27000000ULL;                    /* 1s after wrap: 2s total elapsed */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 2.0, &delta_s), 1);
  ck_assert_double_eq_tol(delta_s, 2.0, 0.01);
}
END_TEST

START_TEST(pcr_plausible_rejects_huge_discontinuity) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10;
  uint64_t new27 = 27000000ULL * 10000; /* implies ~9990s elapsed */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 0);
}
END_TEST

START_TEST(pcr_plausible_rejects_backward_jump) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 100;
  uint64_t new27 = 27000000ULL * 50; /* wraparound-"corrects" to a huge forward delta */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 0);
}
END_TEST

START_TEST(pcr_plausible_rejects_zero_wall_delta) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10;
  uint64_t new27 = 27000000ULL * 11;
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 0.0, &delta_s), 0);
}
END_TEST

START_TEST(pid_apply_no_target_is_noop) {
  cas_pid_state_t ps;
  memset(&ps, 0, sizeof ps);
  ps.current_parity = 0;
  cas_pid_apply(&ps, 0, 1, 1, 100.0, CAS_FORCE_FLIP_S);
  ck_assert_int_eq(ps.current_parity, 0);
  ck_assert_int_eq(ps.flip_pending, 0);
}
END_TEST

START_TEST(pid_apply_same_target_stays_put) {
  cas_pid_state_t ps;
  memset(&ps, 0, sizeof ps);
  ps.current_parity = 1;
  cas_pid_apply(&ps, 1, 1, 1, 100.0, CAS_FORCE_FLIP_S);
  ck_assert_int_eq(ps.current_parity, 1);
  ck_assert_int_eq(ps.flip_pending, 0);
}
END_TEST

START_TEST(pid_apply_flip_lands_exactly_on_pusi) {
  cas_pid_state_t ps;
  int i;
  memset(&ps, 0, sizeof ps);
  ps.current_parity = 0;

  /* target flips to 1; several PUSI=0 packets must not flip it yet */
  for (i = 0; i < 5; i++) {
    cas_pid_apply(&ps, 1, 1, 0, 100.0 + i * 0.01, CAS_FORCE_FLIP_S);
    ck_assert_int_eq(ps.current_parity, 0);
    ck_assert_int_eq(ps.flip_pending, 1);
  }
  /* first PUSI=1 packet: flip lands exactly here */
  cas_pid_apply(&ps, 1, 1, 1, 100.06, CAS_FORCE_FLIP_S);
  ck_assert_int_eq(ps.current_parity, 1);
  ck_assert_int_eq(ps.flip_pending, 0);
}
END_TEST

START_TEST(pid_apply_forced_after_deadline_without_pusi) {
  cas_pid_state_t ps;
  memset(&ps, 0, sizeof ps);
  ps.current_parity = 0;

  cas_pid_apply(&ps, 1, 1, 0, 100.0, 2.0); /* pending set, deadline = 102.0 */
  ck_assert_int_eq(ps.current_parity, 0);
  ck_assert_int_eq(ps.flip_pending, 1);

  cas_pid_apply(&ps, 1, 1, 0, 101.9, 2.0); /* still before deadline, no PUSI: no flip */
  ck_assert_int_eq(ps.current_parity, 0);

  cas_pid_apply(&ps, 1, 1, 0, 102.1, 2.0); /* past deadline, still no PUSI: forced */
  ck_assert_int_eq(ps.current_parity, 1);
  ck_assert_int_eq(ps.flip_pending, 0);
}
END_TEST

START_TEST(pid_apply_retargets_while_pending_without_extending_deadline) {
  cas_pid_state_t ps;
  memset(&ps, 0, sizeof ps);
  ps.current_parity = 0;

  cas_pid_apply(&ps, 1, 1, 0, 100.0, 2.0); /* pending, target 1, deadline 102.0 */
  ck_assert_double_eq_tol(ps.flip_deadline_wall, 102.0, 1e-9);

  /* target changes again to 0 while still pending: deadline must not reset */
  cas_pid_apply(&ps, 1, 0, 0, 101.0, 2.0);
  ck_assert_double_eq_tol(ps.flip_deadline_wall, 102.0, 1e-9);

  /* forced flip at the ORIGINAL deadline lands on the LATEST target (0), not the first (1) */
  cas_pid_apply(&ps, 1, 0, 0, 102.1, 2.0);
  ck_assert_int_eq(ps.current_parity, 0);
  ck_assert_int_eq(ps.flip_pending, 0);
}
END_TEST

static Suite *cas_suite(void) {
  Suite *s = suite_create("cas");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_pcr_recovers_known_value);
  tcase_add_test(tc, parse_pcr_zero_extension);
  tcase_add_test(tc, parse_pcr_no_adaptation_field);
  tcase_add_test(tc, parse_pcr_flag_not_set);
  tcase_add_test(tc, parse_pcr_adaptation_field_too_short);
  tcase_add_test(tc, pcr_plausible_normal_delta_accepted);
  tcase_add_test(tc, pcr_plausible_wraparound_accepted);
  tcase_add_test(tc, pcr_plausible_rejects_huge_discontinuity);
  tcase_add_test(tc, pcr_plausible_rejects_backward_jump);
  tcase_add_test(tc, pcr_plausible_rejects_zero_wall_delta);
  tcase_add_test(tc, pid_apply_no_target_is_noop);
  tcase_add_test(tc, pid_apply_same_target_stays_put);
  tcase_add_test(tc, pid_apply_flip_lands_exactly_on_pusi);
  tcase_add_test(tc, pid_apply_forced_after_deadline_without_pusi);
  tcase_add_test(tc, pid_apply_retargets_while_pending_without_extending_deadline);
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
