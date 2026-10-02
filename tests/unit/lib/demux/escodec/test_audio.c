/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/bitwriter.h"
#include "lib/demux/escodec/escodec.h"

static size_t build_latm_frame(unsigned char *out, size_t cap, unsigned aot, unsigned sr_idx, unsigned ch, int hierarchical, unsigned ext_aot) {
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;
  size_t total;
  bitwriter_init(&bw);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 6);
  bitwriter_put(&bw, 0, 4);
  bitwriter_put(&bw, 0, 3);
  if (hierarchical) {
    bitwriter_put(&bw, ext_aot, 5);
    bitwriter_put(&bw, sr_idx, 4);
    bitwriter_put(&bw, ch, 4);
    bitwriter_put(&bw, 3, 4);
    bitwriter_put(&bw, aot, 5);
  } else {
    bitwriter_put(&bw, aot, 5);
    bitwriter_put(&bw, sr_idx, 4);
    bitwriter_put(&bw, ch, 4);
  }
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 0, 8);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 4, 8);
  bitwriter_put(&bw, 0xAA, 8);
  bitwriter_put(&bw, 0xBB, 8);
  bitwriter_put(&bw, 0xCC, 8);
  bitwriter_put(&bw, 0xDD, 8);

  payload = bitwriter_data(&bw, &plen);
  total = 3 + plen;
  if (total > cap) {
    bitwriter_free(&bw);
    return 0;
  }
  out[0] = 0x56;
  out[1] = (unsigned char)(0xE0 | ((plen >> 8) & 0x1F));
  out[2] = (unsigned char)plen;
  memcpy(out + 3, payload, plen);
  bitwriter_free(&bw);
  return total;
}

START_TEST(aac_latm_plain_config_reports_rate_channels) {
  unsigned char d[32];
  size_t len = build_latm_frame(d, sizeof d, 2, 4, 2, 0, 0);
  esc_track_t t;
  esc_frame_t f;
  int r;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC_LATM;
  r = next_frame(&t, d, len, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, len);
  ck_assert_uint_eq(f.rate, 44100);
  ck_assert_uint_eq(f.ch, 2);
  ck_assert_uint_eq(f.samples, 1024);
  ck_assert_uint_eq(f.outlen, 4);
  ck_assert_int_eq(memcmp(f.out, "\xAA\xBB\xCC\xDD", 4), 0);
}
END_TEST

START_TEST(aac_latm_hierarchical_sbr_reports_core_rate_channels) {
  unsigned char d[32];
  size_t len = build_latm_frame(d, sizeof d, 2, 6, 2, 1, 5);
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC_LATM;
  r = next_frame(&t, d, len, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.rate, 24000);
  ck_assert_uint_eq(f.ch, 2);
  ck_assert_uint_eq(f.samples, 1024);
}
END_TEST

START_TEST(dts_core_frame_reports_rate_channels_samples) {
  /* ETSI TS 102 114 clause 5.3: sync 0x7FFE8001, NBLKS=15, FSIZE=96, AMODE=2 (stereo), SFREQ=13 (48000 Hz) */
  unsigned char d[97] = {0x7F, 0xFE, 0x80, 0x01, 0x00, 0x3C, 0x06, 0x00, 0xB4};
  esc_track_t t;
  esc_frame_t f;
  int r;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_DTS;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 97);
  ck_assert_uint_eq(f.rate, 48000);
  ck_assert_uint_eq(f.ch, 2);
  ck_assert_uint_eq(f.samples, 512);
}
END_TEST

START_TEST(dts_core_frame_needs_more_data_when_truncated) {
  unsigned char d[9] = {0x7F, 0xFE, 0x80, 0x01, 0x00, 0x3C, 0x06, 0x00, 0xB4};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_DTS;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 1);
}
END_TEST

START_TEST(dts_core_frame_rejects_bad_sync) {
  unsigned char d[97] = {0x00, 0xFE, 0x80, 0x01, 0x00, 0x3C, 0x06, 0x00, 0xB4};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_DTS;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, -1);
}
END_TEST

START_TEST(truehd_major_sync_reports_rate_channels_atmos) {
  unsigned char d[22] = {
      0x00, 0x0B,             /* AU length: 11*2 = 22 */
      0x00, 0x00,             /* input timing, unused */
      0xF8, 0x72, 0x6F, 0xBA, /* major sync, TrueHD */
      0x00, 0x00, 0x80, 0x03, /* ratebits..channel_arrangement2 */
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* skip_bits_long(48) */
      0x00, 0x00,             /* is_vbr + peak_bitrate (16 bits) */
      0x40,                   /* num_substreams=4 + skip(2) + extended_substream_info(2) */
      0x81                    /* substream_info: top bit set */
  };
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_TRUEHD;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 22);
  ck_assert_uint_eq(f.rate, 48000);
  ck_assert_uint_eq(f.ch, 3);
  ck_assert_uint_eq(f.samples, 40);
  ck_assert_int_eq(f.atmos, 1);
  ck_assert_uint_eq(f.truehd_format_info, 0x00008003u);
  ck_assert_uint_eq(f.truehd_peak_data_rate, 0x0000u);
}
END_TEST

START_TEST(truehd_frame_without_major_sync_fails_until_config_seen) {
  unsigned char d[8] = {0x00, 0x04, 0x00, 0x00, 0xAA, 0xBB, 0xCC, 0xDD};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_TRUEHD;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, -1);
}
END_TEST

START_TEST(truehd_non_sync_frame_reuses_cached_config) {
  unsigned char sync_d[22] = {
    0x00, 0x0B, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA,
    0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x81
  };
  unsigned char non_sync_d[8] = {0x00, 0x04, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_TRUEHD;
  r = next_frame(&t, sync_d, sizeof sync_d, &f);
  ck_assert_int_eq(r, 0);

  r = next_frame(&t, non_sync_d, sizeof non_sync_d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 8);
  ck_assert_uint_eq(f.rate, 48000);
  ck_assert_uint_eq(f.ch, 3);
  ck_assert_int_eq(f.atmos, 0);
}
END_TEST

START_TEST(ac4_mono_iframe_reports_rate_samples_channels) {
  unsigned char d[10] = {0xac, 0x40, 0x00, 0x06, 0x00, 0x05, 0xb4, 0x00, 0x00, 0x00};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 10);
  ck_assert_uint_eq(f.rate, 48000);
  ck_assert_uint_eq(f.samples, 960);
  ck_assert_uint_eq(f.ch, 1);
  ck_assert_int_eq(f.ac4_iframe, 1);
  ck_assert_uint_eq(f.outlen, 6);
}
END_TEST

START_TEST(ac4_stereo_non_iframe_reports_channels) {
  unsigned char d[10] = {0xac, 0x40, 0x00, 0x06, 0x00, 0x05, 0x94, 0x00, 0x04, 0x00};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 10);
  ck_assert_uint_eq(f.ch, 2);
  ck_assert_int_eq(f.ac4_iframe, 0);
}
END_TEST

START_TEST(ac4_5_1_reports_six_channels) {
  unsigned char d[11] = {0xac, 0x40, 0x00, 0x07, 0x00, 0x05, 0xb4, 0x00, 0x07, 0x00, 0x00};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 0);
  ck_assert_uint_eq(f.consumed, 11);
  ck_assert_uint_eq(f.ch, 6);
  ck_assert_int_eq(f.ac4_iframe, 1);
}
END_TEST

START_TEST(ac4_frame_needs_more_data_when_truncated) {
  unsigned char d[5] = {0xac, 0x40, 0x00, 0x06, 0x00};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, 1);
}
END_TEST

START_TEST(ac4_frame_rejects_bad_sync) {
  unsigned char d[10] = {0xac, 0x00, 0x00, 0x06, 0x00, 0x05, 0xb4, 0x00, 0x00, 0x00};
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, sizeof d, &f);
  ck_assert_int_eq(r, -1);
}
END_TEST

#define FRAME_BUF_MAX 512
#define FRAME_HDR_MAX 8

typedef struct {
  const char *name;
  codec_t codec;
  unsigned char hdr[FRAME_HDR_MAX];
  size_t len;
  int expect;
} frame_case_t;

static const frame_case_t frame_cases[] = {
    {"ac3 valid", CODEC_AC3, {0x0B, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00}, 128, 0},
    {"ac3 bad sync first byte", CODEC_AC3, {0x00, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00}, 128, -1},
    {"ac3 bad sync second byte", CODEC_AC3, {0x0B, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00}, 128, -1},
    {"ac3 reserved fscod", CODEC_AC3, {0x0B, 0x77, 0x00, 0x00, 0xC0, 0x00, 0x00}, 128, -1},
    {"ac3 frmsizecod out of range", CODEC_AC3, {0x0B, 0x77, 0x00, 0x00, 0x26, 0x00, 0x00}, 128, -1},
    {"ac3 header truncated", CODEC_AC3, {0x0B, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 1},
    {"ac3 frame truncated", CODEC_AC3, {0x0B, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00}, 100, 1},
    {"eac3 valid", CODEC_EAC3, {0x0B, 0x77, 0x00, 0x3F, 0x00, 0x00}, 128, 0},
    {"eac3 bad sync", CODEC_EAC3, {0x0B, 0x00, 0x00, 0x3F, 0x00, 0x00}, 128, -1},
    {"eac3 frame size too small", CODEC_EAC3, {0x0B, 0x77, 0x00, 0x00, 0x00, 0x00}, 128, -1},
    {"eac3 reserved numblkscod", CODEC_EAC3, {0x0B, 0x77, 0x00, 0x3F, 0xF0, 0x00}, 128, -1},
    {"eac3 header truncated", CODEC_EAC3, {0x0B, 0x77, 0x00, 0x3F, 0x00, 0x00}, 5, 1},
    {"eac3 frame truncated", CODEC_EAC3, {0x0B, 0x77, 0x00, 0x3F, 0x00, 0x00}, 64, 1},
    {"mpeg audio valid", CODEC_MP2A, {0xFF, 0xFB, 0x90, 0x00}, 417, 0},
    {"mpeg audio bad sync first byte", CODEC_MP2A, {0x00, 0xFB, 0x90, 0x00}, 417, -1},
    {"mpeg audio bad sync second byte", CODEC_MP2A, {0xFF, 0x1B, 0x90, 0x00}, 417, -1},
    {"mpeg audio reserved version", CODEC_MP2A, {0xFF, 0xEB, 0x90, 0x00}, 417, -1},
    {"mpeg audio free bitrate", CODEC_MP2A, {0xFF, 0xFB, 0x00, 0x00}, 417, -1},
    {"mpeg audio bad bitrate", CODEC_MP2A, {0xFF, 0xFB, 0xF0, 0x00}, 417, -1},
    {"mpeg audio reserved rate", CODEC_MP2A, {0xFF, 0xFB, 0x9C, 0x00}, 417, -1},
    {"mpeg audio header truncated", CODEC_MP2A, {0xFF, 0xFB, 0x90, 0x00}, 3, 1},
    {"mpeg audio frame truncated", CODEC_MP2A, {0xFF, 0xFB, 0x90, 0x00}, 100, 1},
    {"aac adts valid", CODEC_AAC, {0xFF, 0xF1, 0x50, 0x80, 0x10, 0x00, 0x00}, 128, 0},
    {"aac adts bad sync first byte", CODEC_AAC, {0x00, 0xF1, 0x50, 0x80, 0x10, 0x00, 0x00}, 128, -1},
    {"aac adts bad sync second byte", CODEC_AAC, {0xFF, 0xF2, 0x50, 0x80, 0x10, 0x00, 0x00}, 128, -1},
    {"aac adts bad sample rate index", CODEC_AAC, {0xFF, 0xF1, 0x34, 0x80, 0x10, 0x00, 0x00}, 128, -1},
    {"aac adts frame shorter than header", CODEC_AAC, {0xFF, 0xF1, 0x50, 0x80, 0x00, 0x00, 0x00}, 128, -1},
    {"aac adts header truncated", CODEC_AAC, {0xFF, 0xF1, 0x50, 0x80, 0x10, 0x00, 0x00}, 6, 1},
    {"aac adts frame truncated", CODEC_AAC, {0xFF, 0xF1, 0x50, 0x80, 0x10, 0x00, 0x00}, 100, 1},
    {"truehd header truncated", CODEC_TRUEHD, {0x00, 0x10, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA}, 7, 1},
    {"truehd frame truncated", CODEC_TRUEHD, {0x00, 0x10, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA}, 16, 1},
    {"truehd au size below minimum", CODEC_TRUEHD, {0x00, 0x03, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA}, 32, -1},
    {"truehd zero au size", CODEC_TRUEHD, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 32, -1},
};

START_TEST(audio_frame_edge_cases) {
  const frame_case_t *c = &frame_cases[_i];
  unsigned char buf[FRAME_BUF_MAX];
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(buf, 0, sizeof buf);
  memcpy(buf, c->hdr, sizeof c->hdr);
  memset(&t, 0, sizeof t);
  t.codec = c->codec;
  r = next_frame(&t, buf, c->len, &f);
  ck_assert_msg(r == c->expect, "%s: got %d, want %d", c->name, r, c->expect);
}
END_TEST

static Suite *audio_suite(void) {
  Suite *s = suite_create("escodec_audio");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aac_latm_plain_config_reports_rate_channels);
  tcase_add_test(tc, aac_latm_hierarchical_sbr_reports_core_rate_channels);
  tcase_add_test(tc, dts_core_frame_reports_rate_channels_samples);
  tcase_add_test(tc, dts_core_frame_needs_more_data_when_truncated);
  tcase_add_test(tc, dts_core_frame_rejects_bad_sync);
  tcase_add_test(tc, truehd_major_sync_reports_rate_channels_atmos);
  tcase_add_test(tc, truehd_frame_without_major_sync_fails_until_config_seen);
  tcase_add_test(tc, truehd_non_sync_frame_reuses_cached_config);
  tcase_add_test(tc, ac4_mono_iframe_reports_rate_samples_channels);
  tcase_add_test(tc, ac4_stereo_non_iframe_reports_channels);
  tcase_add_test(tc, ac4_5_1_reports_six_channels);
  tcase_add_test(tc, ac4_frame_needs_more_data_when_truncated);
  tcase_add_test(tc, ac4_frame_rejects_bad_sync);
  tcase_add_loop_test(tc, audio_frame_edge_cases, 0, (int)(sizeof frame_cases / sizeof frame_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(audio_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
