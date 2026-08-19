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

static void set_es(out_es_t *es, psi_es_t *psi_es, unsigned out_pid, pid_class_t cls) {
  memset(psi_es, 0, sizeof *psi_es);
  psi_es->cls = cls;
  es->out_pid = out_pid;
  es->src = psi_es;
}

START_TEST(resolve_pids_explicit_only) {
  config_t cfg;
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 2;
  cfg.cas_pids[0] = 0x0104;
  cfg.cas_pids[1] = 0x0106;
  ck_assert_uint_eq(cas_resolve_pids(&cfg, NULL, 0, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0104);
  ck_assert_uint_eq(out[1], 0x0106);
}
END_TEST

START_TEST(resolve_pids_video_keyword) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 1);
  ck_assert_uint_eq(out[0], 0x0100);
}
END_TEST

START_TEST(resolve_pids_audio_keyword_multiple) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0102, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0101);
  ck_assert_uint_eq(out[1], 0x0102);
}
END_TEST

START_TEST(resolve_pids_mixed_explicit_and_keyword_dedupes) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 1;
  cfg.cas_pids[0] = 0x0100; /* same as the video out_pid below */
  cfg.cas_pids_video = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 1);
  ck_assert_uint_eq(out[0], 0x0100);
}
END_TEST

START_TEST(resolve_pids_default_video_and_audio) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0100);
  ck_assert_uint_eq(out[1], 0x0101);
}
END_TEST

START_TEST(resolve_pids_caps_at_limit) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[2];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0101, PID_AUDIO);
  set_es(&es[1], &pe[1], 0x0102, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0103, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 2), 2);
}
END_TEST

START_TEST(resolve_pids_nothing_requested_yields_none) {
  config_t cfg;
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, NULL, 0, out, 16), 0);
}
END_TEST

START_TEST(resolve_pids_exceeds_old_16_limit) {
  config_t cfg;
  psi_es_t pe[20];
  out_es_t es[20];
  unsigned out[CAS_CORE_MAX_PIDS];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  for (int i = 0; i < 20; i++)
    set_es(&es[i], &pe[i], (unsigned)(0x0100 + i), PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 20, out, CAS_CORE_MAX_PIDS), 20);
}
END_TEST

START_TEST(resolve_pids_multi_across_programs) {
  config_t cfg;
  psi_es_t pe0[2], pe1[2];
  out_es_t es0[2], es1[2];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[16];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  cfg.cas_pids_audio = 1;
  set_es(&es0[0], &pe0[0], 0x1100, PID_VIDEO);
  set_es(&es0[1], &pe0[1], 0x1101, PID_AUDIO);
  set_es(&es1[0], &pe1[0], 0x1200, PID_VIDEO);
  set_es(&es1[1], &pe1[1], 0x1201, PID_AUDIO);
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 2;
  es_counts[1] = 2;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 16), 4);
  ck_assert_uint_eq(out[0], 0x1100u);
  ck_assert_uint_eq(out[1], 0x1101u);
  ck_assert_uint_eq(out[2], 0x1200u);
  ck_assert_uint_eq(out[3], 0x1201u);
}
END_TEST

START_TEST(resolve_pids_multi_dedupes_across_programs) {
  config_t cfg;
  psi_es_t pe0[1], pe1[1];
  out_es_t es0[1], es1[1];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[16];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 1;
  cfg.cas_pids[0] = 0x1300; /* same pid program 1 also resolves via --cas-pids-video */
  cfg.cas_pids_video = 1;
  set_es(&es0[0], &pe0[0], 0x1400, PID_VIDEO);
  set_es(&es1[0], &pe1[0], 0x1300, PID_VIDEO);
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 1;
  es_counts[1] = 1;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x1300u);
  ck_assert_uint_eq(out[1], 0x1400u);
}
END_TEST

START_TEST(resolve_pids_multi_caps_across_programs) {
  config_t cfg;
  psi_es_t pe0[3], pe1[3];
  out_es_t es0[3], es1[3];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[4];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  for (int i = 0; i < 3; i++) {
    set_es(&es0[i], &pe0[i], (unsigned)(0x1500 + i), PID_AUDIO);
    set_es(&es1[i], &pe1[i], (unsigned)(0x1600 + i), PID_AUDIO);
  }
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 3;
  es_counts[1] = 3;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 4), 4);
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
  tcase_add_test(tc, resolve_pids_explicit_only);
  tcase_add_test(tc, resolve_pids_video_keyword);
  tcase_add_test(tc, resolve_pids_audio_keyword_multiple);
  tcase_add_test(tc, resolve_pids_mixed_explicit_and_keyword_dedupes);
  tcase_add_test(tc, resolve_pids_default_video_and_audio);
  tcase_add_test(tc, resolve_pids_caps_at_limit);
  tcase_add_test(tc, resolve_pids_nothing_requested_yields_none);
  tcase_add_test(tc, resolve_pids_exceeds_old_16_limit);
  tcase_add_test(tc, resolve_pids_multi_across_programs);
  tcase_add_test(tc, resolve_pids_multi_dedupes_across_programs);
  tcase_add_test(tc, resolve_pids_multi_caps_across_programs);
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
