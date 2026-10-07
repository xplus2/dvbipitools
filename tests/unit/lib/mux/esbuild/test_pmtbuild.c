/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/esbuild/pmtbuild.h"

START_TEST(esbuild_assign_pids_sequential_from_first_es_pid) {
  codec_t codecs[3] = {CODEC_H264, CODEC_AAC, CODEC_AC3};
  esbuild_es_t es[3];
  unsigned n = esbuild_assign_pids(codecs, 3, es);

  ck_assert_uint_eq(n, 3u);
  ck_assert_uint_eq(es[0].pid, ESBUILD_FIRST_ES_PID);
  ck_assert_uint_eq(es[1].pid, ESBUILD_FIRST_ES_PID + 1);
  ck_assert_uint_eq(es[2].pid, ESBUILD_FIRST_ES_PID + 2);
}
END_TEST

START_TEST(esbuild_build_pat_points_at_pmt_pid) {
  unsigned char out[64];
  size_t n = esbuild_build_pat(1, 0, 1, out, sizeof out);
  ck_assert_uint_gt(n, 0u);
}
END_TEST

START_TEST(esbuild_build_pmt_uses_video_pid_as_pcr) {
  codec_t codecs[2] = {CODEC_AAC, CODEC_H264};
  esbuild_es_t es[2];
  unsigned char out[188];
  size_t n;
  unsigned pcr_pid;

  esbuild_assign_pids(codecs, 2, es);
  n = esbuild_build_pmt(0, 1, es, 2, out, sizeof out);
  ck_assert_uint_gt(n, 0u);

  pcr_pid = ((unsigned)(out[8] & 0x1F) << 8) | out[9];
  ck_assert_uint_eq(pcr_pid, es[1].pid);
}
END_TEST

START_TEST(esbuild_build_pmt_first_es_stream_type_and_pid) {
  codec_t codecs[1] = {CODEC_H264};
  esbuild_es_t es[1];
  unsigned char out[188];
  size_t n;
  unsigned first_es_pid;

  esbuild_assign_pids(codecs, 1, es);
  n = esbuild_build_pmt(0, 1, es, 1, out, sizeof out);
  ck_assert_uint_gt(n, 0u);

  ck_assert_uint_eq(out[12], 0x1B);
  first_es_pid = ((unsigned)(out[13] & 0x1F) << 8) | out[14];
  ck_assert_uint_eq(first_es_pid, ESBUILD_FIRST_ES_PID);
}
END_TEST

START_TEST(esbuild_build_pmt_emits_registration_descriptor_for_opus) {
  codec_t codecs[1] = {CODEC_OPUS};
  esbuild_es_t es[1];
  unsigned char out[188];
  size_t n;

  esbuild_assign_pids(codecs, 1, es);
  n = esbuild_build_pmt(0, 1, es, 1, out, sizeof out);
  ck_assert_uint_gt(n, 0u);

  ck_assert_uint_eq(out[12], 0x06);
  ck_assert_uint_eq(out[17], 0x05);
  ck_assert_uint_eq(out[18], 4);
  ck_assert_int_eq(memcmp(out + 19, "Opus", 4), 0);
}
END_TEST

START_TEST(esbuild_build_pmt_emits_dvb_ext_descriptor_for_ac4) {
  codec_t codecs[1] = {CODEC_AC4};
  esbuild_es_t es[1];
  unsigned char out[188];
  size_t n;

  esbuild_assign_pids(codecs, 1, es);
  n = esbuild_build_pmt(0, 1, es, 1, out, sizeof out);
  ck_assert_uint_gt(n, 0u);

  ck_assert_uint_eq(out[17], 0x7F);
  ck_assert_uint_eq(out[18], 1);
  ck_assert_uint_eq(out[19], 0x15);
}
END_TEST

START_TEST(esbuild_build_pmt_rejects_zero_es) {
  unsigned char out[188];
  ck_assert_uint_eq(esbuild_build_pmt(0, 1, NULL, 0, out, sizeof out), 0u);
}
END_TEST

typedef struct {
  codec_t codec;
  unsigned construction;
} dts_hd_case_t;

static const dts_hd_case_t dts_hd_cases[] = {
    {CODEC_DTS_HD, 6},
    {CODEC_DTS_HD_MA, 14},
};

START_TEST(esbuild_build_pmt_emits_dts_hd_extension_descriptor) {
  const dts_hd_case_t *c = &dts_hd_cases[_i];
  codec_t codecs[1];
  esbuild_es_t es[1];
  unsigned char out[188];
  size_t n;

  codecs[0] = c->codec;
  esbuild_assign_pids(codecs, 1, es);
  n = esbuild_build_pmt(0, 1, es, 1, out, sizeof out);
  ck_assert_uint_gt(n, 0u);

  ck_assert_uint_eq(out[12], 0x06);
  ck_assert_uint_eq(out[16], 10);
  ck_assert_uint_eq(out[17], 0x7F);
  ck_assert_uint_eq(out[18], 8);
  ck_assert_uint_eq(out[19], 0x0E);
  ck_assert_uint_eq(out[20], 0x80);
  ck_assert_uint_eq(out[21], 5);
  ck_assert_uint_eq(out[24], c->construction << 3);
}
END_TEST

static Suite *esbuild_pmtbuild_suite(void) {
  Suite *s = suite_create("esbuild_pmtbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, esbuild_assign_pids_sequential_from_first_es_pid);
  tcase_add_test(tc, esbuild_build_pat_points_at_pmt_pid);
  tcase_add_test(tc, esbuild_build_pmt_uses_video_pid_as_pcr);
  tcase_add_test(tc, esbuild_build_pmt_first_es_stream_type_and_pid);
  tcase_add_test(tc, esbuild_build_pmt_emits_registration_descriptor_for_opus);
  tcase_add_test(tc, esbuild_build_pmt_emits_dvb_ext_descriptor_for_ac4);
  tcase_add_loop_test(tc, esbuild_build_pmt_emits_dts_hd_extension_descriptor, 0, (int)(sizeof dts_hd_cases / sizeof dts_hd_cases[0]));
  tcase_add_test(tc, esbuild_build_pmt_rejects_zero_es);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(esbuild_pmtbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
