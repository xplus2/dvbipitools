/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/esbuild/esbuild.h"

static void avcc_append(unsigned char *buf, size_t *pos, const unsigned char *nal, size_t len) {
  buf[(*pos)++] = (unsigned char)(len >> 24);
  buf[(*pos)++] = (unsigned char)(len >> 16);
  buf[(*pos)++] = (unsigned char)(len >> 8);
  buf[(*pos)++] = (unsigned char)len;
  memcpy(buf + *pos, nal, len);
  *pos += len;
}

START_TEST(esbuild_h264_idr_gets_sps_pps_reinjected) {
  static const unsigned char sps[] = {0x67, 0x64, 0x00, 0x1F};
  static const unsigned char pps[] = {0x68, 0xEB};
  static const unsigned char idr[] = {0x65, 0x88, 0x99};
  fmp4_stsd_entry_t stsd;
  esbuild_track_t t;
  unsigned char avcc[64];
  size_t apos = 0;
  fmp4_dec_sample_t sample;
  unsigned char out[128];
  size_t n;
  static const unsigned char expect_start[4] = {0, 0, 0, 1};

  memset(&stsd, 0, sizeof stsd);
  stsd.codec = CODEC_H264;
  stsd.sps[0].data = sps;
  stsd.sps[0].len = sizeof sps;
  stsd.n_sps = 1;
  stsd.pps[0].data = pps;
  stsd.pps[0].len = sizeof pps;
  stsd.n_pps = 1;
  esbuild_track_init(&t, &stsd);

  avcc_append(avcc, &apos, idr, sizeof idr);
  memset(&sample, 0, sizeof sample);
  sample.data = avcc;
  sample.size = apos;

  n = esbuild_convert_sample(&t, &sample, out, sizeof out);
  /* SPS: start(4)+4B, PPS: start(4)+2B, IDR: start(4)+3B */
  ck_assert_uint_eq(n, (4 + sizeof sps) + (4 + sizeof pps) + (4 + sizeof idr));
  ck_assert_int_eq(memcmp(out, expect_start, 4), 0);
  ck_assert_int_eq(memcmp(out + 4, sps, sizeof sps), 0);
  ck_assert_int_eq(memcmp(out + 4 + sizeof sps, expect_start, 4), 0);
  ck_assert_int_eq(memcmp(out + 4 + sizeof sps + 4, pps, sizeof pps), 0);
  ck_assert_int_eq(memcmp(out + 4 + sizeof sps + 4 + sizeof pps, expect_start, 4), 0);
  ck_assert_int_eq(memcmp(out + 4 + sizeof sps + 4 + sizeof pps + 4, idr, sizeof idr), 0);
}
END_TEST

START_TEST(esbuild_h264_non_idr_gets_no_reinjection) {
  static const unsigned char sps[] = {0x67, 0x64};
  static const unsigned char pframe[] = {0x41, 0x9A};
  fmp4_stsd_entry_t stsd;
  esbuild_track_t t;
  unsigned char avcc[64];
  size_t apos = 0;
  fmp4_dec_sample_t sample;
  unsigned char out[128];
  size_t n;
  static const unsigned char expect_start[4] = {0, 0, 0, 1};

  memset(&stsd, 0, sizeof stsd);
  stsd.codec = CODEC_H264;
  stsd.sps[0].data = sps;
  stsd.sps[0].len = sizeof sps;
  stsd.n_sps = 1;
  esbuild_track_init(&t, &stsd);

  avcc_append(avcc, &apos, pframe, sizeof pframe);
  memset(&sample, 0, sizeof sample);
  sample.data = avcc;
  sample.size = apos;

  n = esbuild_convert_sample(&t, &sample, out, sizeof out);
  ck_assert_uint_eq(n, 4 + sizeof pframe);
  ck_assert_int_eq(memcmp(out, expect_start, 4), 0);
  ck_assert_int_eq(memcmp(out + 4, pframe, sizeof pframe), 0);
}
END_TEST

START_TEST(esbuild_hevc_idr_type19_triggers_reinjection) {
  static const unsigned char vps[] = {0x40, 0x01};
  static const unsigned char sps[] = {0x42, 0x01};
  static const unsigned char pps[] = {0x44, 0x01};
  static unsigned char idr[2];
  fmp4_stsd_entry_t stsd;
  esbuild_track_t t;
  unsigned char avcc[64];
  size_t apos = 0;
  fmp4_dec_sample_t sample;
  unsigned char out[128];
  size_t n;

  idr[0] = (unsigned char)(19 << 1); /* HEVC NAL header: type in bits 6-1 of byte0 */
  idr[1] = 0x01;

  memset(&stsd, 0, sizeof stsd);
  stsd.codec = CODEC_HEVC;
  stsd.vps[0].data = vps;
  stsd.vps[0].len = sizeof vps;
  stsd.n_vps = 1;
  stsd.sps[0].data = sps;
  stsd.sps[0].len = sizeof sps;
  stsd.n_sps = 1;
  stsd.pps[0].data = pps;
  stsd.pps[0].len = sizeof pps;
  stsd.n_pps = 1;
  esbuild_track_init(&t, &stsd);

  avcc_append(avcc, &apos, idr, sizeof idr);
  memset(&sample, 0, sizeof sample);
  sample.data = avcc;
  sample.size = apos;

  n = esbuild_convert_sample(&t, &sample, out, sizeof out);
  ck_assert_uint_eq(n, (4 + sizeof vps) + (4 + sizeof sps) + (4 + sizeof pps) + (4 + sizeof idr));
}
END_TEST

START_TEST(esbuild_aac_synthesizes_adts_header) {
  fmp4_stsd_entry_t stsd;
  esbuild_track_t t;
  static const unsigned char asc[] = {0x11, 0x90}; /* AOT=2(LC), sfi=3(48000), channels=2 */
  static const unsigned char payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  fmp4_dec_sample_t sample;
  unsigned char out[64];
  size_t n;

  memset(&stsd, 0, sizeof stsd);
  stsd.codec = CODEC_AAC;
  stsd.cpriv = asc;
  stsd.cpriv_len = sizeof asc;
  esbuild_track_init(&t, &stsd);
  ck_assert_uint_eq(t.aac_object_type, 2u);
  ck_assert_uint_eq(t.aac_sr_index, 3u);
  ck_assert_uint_eq(t.aac_channels, 2u);

  memset(&sample, 0, sizeof sample);
  sample.data = payload;
  sample.size = sizeof payload;
  n = esbuild_convert_sample(&t, &sample, out, sizeof out);

  ck_assert_uint_eq(n, 17u);
  ck_assert_uint_eq(out[0], 0xFF);
  ck_assert_uint_eq(out[1], 0xF1);
  ck_assert_uint_eq(out[2], 0x4C);
  ck_assert_uint_eq(out[3], 0x80);
  ck_assert_uint_eq(out[4], 2);
  ck_assert_uint_eq(out[5], 0x3F);
  ck_assert_uint_eq(out[6], 0xFC);
  ck_assert_int_eq(memcmp(out + 7, payload, sizeof payload), 0);
}
END_TEST

START_TEST(esbuild_ac3_passes_through_unchanged) {
  fmp4_stsd_entry_t stsd;
  esbuild_track_t t;
  static const unsigned char raw[] = {0x0B, 0x77, 0xAA, 0xBB, 0xCC};
  fmp4_dec_sample_t sample;
  unsigned char out[32];
  size_t n;

  memset(&stsd, 0, sizeof stsd);
  stsd.codec = CODEC_AC3;
  esbuild_track_init(&t, &stsd);

  memset(&sample, 0, sizeof sample);
  sample.data = raw;
  sample.size = sizeof raw;
  n = esbuild_convert_sample(&t, &sample, out, sizeof out);

  ck_assert_uint_eq(n, sizeof raw);
  ck_assert_int_eq(memcmp(out, raw, sizeof raw), 0);
}
END_TEST

static Suite *esbuild_suite(void) {
  Suite *s = suite_create("esbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, esbuild_h264_idr_gets_sps_pps_reinjected);
  tcase_add_test(tc, esbuild_h264_non_idr_gets_no_reinjection);
  tcase_add_test(tc, esbuild_hevc_idr_type19_triggers_reinjection);
  tcase_add_test(tc, esbuild_aac_synthesizes_adts_header);
  tcase_add_test(tc, esbuild_ac3_passes_through_unchanged);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(esbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
