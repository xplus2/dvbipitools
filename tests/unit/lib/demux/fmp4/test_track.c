/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/fmp4/track.h"
#include "lib/mux/fmp4/box.h"

static void build_and_find_entry(const trak_meta_t *t, unsigned char *buf, size_t bufcap, fmp4_box_t *entry_out) {
  mp4buf_t stsd;
  fmp4_box_t stsd_box, e;
  const char *entry_fourcc;
  memset(&stsd, 0, sizeof stsd);
  trak_build_stsd(&stsd, t);
  ck_assert_int_eq(stsd.err, 0);
  ck_assert_uint_le(stsd.len, bufcap);
  memcpy(buf, stsd.p, stsd.len);
  mp4buf_free(&stsd);

  ck_assert_int_eq(fmp4_box_read(buf, buf + bufcap, &stsd_box), 1);
  ck_assert_str_eq(stsd_box.fourcc, "stsd");
  /* stsd body: version(1)+flags(3)+entry_count(4), one entry */
  ck_assert_int_eq(fmp4_box_read(stsd_box.body + 8, stsd_box.body + stsd_box.body_len, &e), 1);
  entry_fourcc = e.fourcc;
  (void)entry_fourcc;
  *entry_out = e;
}

START_TEST(fmp4_parse_stsd_entry_h264_avcc_extracts_sps_pps) {
  static const unsigned char sps[] = {0x67, 0x64, 0x00, 0x1F, 0xAA, 0xBB, 0xCC};
  static const unsigned char pps[] = {0x68, 0xEB, 0x8F, 0x2C};
  unsigned char cpriv[64];
  size_t n = 0;
  trak_meta_t t;
  unsigned char buf[512];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  /* mirror build_avcc() layout */
  cpriv[n++] = 1;
  cpriv[n++] = sps[1];
  cpriv[n++] = sps[2];
  cpriv[n++] = sps[3];
  cpriv[n++] = 0xFF;
  cpriv[n++] = 0xE1;
  cpriv[n++] = 0;
  cpriv[n++] = (unsigned char)sizeof sps;
  memcpy(cpriv + n, sps, sizeof sps);
  n += sizeof sps;
  cpriv[n++] = 1;
  cpriv[n++] = 0;
  cpriv[n++] = (unsigned char)sizeof pps;
  memcpy(cpriv + n, pps, sizeof pps);
  n += sizeof pps;

  memset(&t, 0, sizeof t);
  t.track_id = 1;
  t.cls = PID_VIDEO;
  t.codec = CODEC_H264;
  t.width = 1920;
  t.height = 1080;
  t.cpriv = cpriv;
  t.cpriv_len = n;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "avc1");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_H264);
  ck_assert_uint_eq(out.width, 1920u);
  ck_assert_uint_eq(out.height, 1080u);
  ck_assert_uint_eq(out.n_sps, 1u);
  ck_assert_uint_eq(out.sps[0].len, sizeof sps);
  ck_assert_int_eq(memcmp(out.sps[0].data, sps, sizeof sps), 0);
  ck_assert_uint_eq(out.n_pps, 1u);
  ck_assert_uint_eq(out.pps[0].len, sizeof pps);
  ck_assert_int_eq(memcmp(out.pps[0].data, pps, sizeof pps), 0);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_hevc_hvcc_extracts_vps_sps_pps) {
  static const unsigned char vps[] = {0x40, 0x01, 0x0C};
  static const unsigned char sps[] = {0x42, 0x01, 0x01, 0x02, 0x03};
  static const unsigned char pps[] = {0x44, 0x01, 0xC1};
  unsigned char cpriv[128];
  size_t n = 0;
  int a;
  static const unsigned char types[3] = {32, 33, 34};
  const unsigned char *ps[3] = {vps, sps, pps};
  size_t pl[3] = {sizeof vps, sizeof sps, sizeof pps};
  trak_meta_t t;
  unsigned char buf[512];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  /* mirrors build_hvcc()'s own layout */
  cpriv[n++] = 1;
  for (int i = 0; i < 12; i++) cpriv[n++] = 0;
  cpriv[n++] = 0xF0;
  cpriv[n++] = 0x00;
  cpriv[n++] = 0xFC;
  cpriv[n++] = 0xFC;
  cpriv[n++] = 0xF8;
  cpriv[n++] = 0xF8;
  cpriv[n++] = 0x00;
  cpriv[n++] = 0x00;
  cpriv[n++] = 0x03;
  cpriv[n++] = 3;
  for (a = 0; a < 3; a++) {
    cpriv[n++] = types[a];
    cpriv[n++] = 0;
    cpriv[n++] = 1;
    cpriv[n++] = (unsigned char)(pl[a] >> 8);
    cpriv[n++] = (unsigned char)pl[a];
    memcpy(cpriv + n, ps[a], pl[a]);
    n += pl[a];
  }

  memset(&t, 0, sizeof t);
  t.track_id = 1;
  t.cls = PID_VIDEO;
  t.codec = CODEC_HEVC;
  t.width = 1920;
  t.height = 1080;
  t.cpriv = cpriv;
  t.cpriv_len = n;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "hvc1");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_HEVC);
  ck_assert_uint_eq(out.n_vps, 1u);
  ck_assert_int_eq(memcmp(out.vps[0].data, vps, sizeof vps), 0);
  ck_assert_uint_eq(out.n_sps, 1u);
  ck_assert_int_eq(memcmp(out.sps[0].data, sps, sizeof sps), 0);
  ck_assert_uint_eq(out.n_pps, 1u);
  ck_assert_int_eq(memcmp(out.pps[0].data, pps, sizeof pps), 0);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_vvc_vvcc_extracts_vps_sps_pps) {
  static const unsigned char vps[] = {0x00, 0x38, 0x01};
  static const unsigned char sps[] = {0x00, 0x79, 0x01, 0x02};
  static const unsigned char pps[] = {0x00, 0x88, 0xC1};
  unsigned char cpriv[128];
  size_t n = 0;
  int a;
  static const unsigned char types[3] = {14, 15, 16};
  const unsigned char *ps[3] = {vps, sps, pps};
  size_t pl[3] = {sizeof vps, sizeof sps, sizeof pps};
  trak_meta_t t;
  unsigned char buf[512];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  /* mirrors build_vvcc()'s own layout */
  cpriv[n++] = 0xFE;
  cpriv[n++] = 3;
  for (a = 0; a < 3; a++) {
    cpriv[n++] = (unsigned char)(0x80 | types[a]);
    cpriv[n++] = 0;
    cpriv[n++] = 1;
    cpriv[n++] = (unsigned char)(pl[a] >> 8);
    cpriv[n++] = (unsigned char)pl[a];
    memcpy(cpriv + n, ps[a], pl[a]);
    n += pl[a];
  }

  memset(&t, 0, sizeof t);
  t.track_id = 1;
  t.cls = PID_VIDEO;
  t.codec = CODEC_VVC;
  t.width = 3840;
  t.height = 2160;
  t.cpriv = cpriv;
  t.cpriv_len = n;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "vvc1");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_VVC);
  ck_assert_uint_eq(out.n_vps, 1u);
  ck_assert_int_eq(memcmp(out.vps[0].data, vps, sizeof vps), 0);
  ck_assert_uint_eq(out.n_sps, 1u);
  ck_assert_int_eq(memcmp(out.sps[0].data, sps, sizeof sps), 0);
  ck_assert_uint_eq(out.n_pps, 1u);
  ck_assert_int_eq(memcmp(out.pps[0].data, pps, sizeof pps), 0);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_ac3_extracts_dac3_fields) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_AC3;
  t.rate = 48000;
  t.channels = 6;
  t.ac3_bsid = 8;
  t.ac3_bsmod = 3;
  t.ac3_acmod = 7;
  t.ac3_lfeon = 1;
  t.ac3_bitrate_code = 21;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "ac-3");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_AC3);
  ck_assert_uint_eq(out.channels, 6u);
  ck_assert_uint_eq(out.rate, 48000u);
  ck_assert_uint_eq(out.ac3_bsid, 8u);
  ck_assert_uint_eq(out.ac3_bsmod, 3u);
  ck_assert_uint_eq(out.ac3_acmod, 7u);
  ck_assert_uint_eq(out.ac3_lfeon, 1u);
  ck_assert_uint_eq(out.ac3_bitrate_code, 21u);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_eac3_extracts_dec3_fields) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_EAC3;
  t.rate = 48000;
  t.channels = 6;
  t.ac3_bsid = 16;
  t.ac3_bsmod = 2;
  t.ac3_acmod = 5;
  t.ac3_lfeon = 0;
  t.ac3_bitrate_code = 640;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "ec-3");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_EAC3);
  ck_assert_uint_eq(out.ac3_bitrate_code, 640u);
  ck_assert_uint_eq(out.ac3_bsid, 16u);
  ck_assert_uint_eq(out.ac3_bsmod, 2u);
  ck_assert_uint_eq(out.ac3_acmod, 5u);
  ck_assert_uint_eq(out.ac3_lfeon, 0u);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_opus_extracts_channels) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_OPUS;
  t.rate = 48000;
  t.channels = 2;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "Opus");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_OPUS);
  ck_assert_uint_eq(out.channels, 2u);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_dts_hd_ma_extracts_rate) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_DTS_HD_MA;
  t.rate = 96000;
  t.channels = 8;
  t.dts_has_core = 1;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "dtsh");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_uint_eq(out.dts_rate, 96000u);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_truehd_extracts_dmlp_fields) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_TRUEHD;
  t.rate = 48000;
  t.channels = 8;
  t.truehd_format_info = 0xC1;
  t.truehd_peak_data_rate = 1500;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "mlpa");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_uint_eq(out.truehd_format_info, 0xC1u);
  ck_assert_uint_eq(out.truehd_peak_data_rate, 1500u);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_ac4_extracts_opaque_cpriv) {
  static const unsigned char raw[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_AC4;
  t.rate = 48000;
  t.channels = 6;
  t.cpriv = raw;
  t.cpriv_len = sizeof raw;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "ac-4");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_uint_eq(out.cpriv_len, sizeof raw);
  ck_assert_int_eq(memcmp(out.cpriv, raw, sizeof raw), 0);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_aac_extracts_asc_via_esds) {
  static const unsigned char asc[] = {0x12, 0x10};
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_AAC;
  t.rate = 48000;
  t.channels = 2;
  t.cpriv = asc;
  t.cpriv_len = sizeof asc;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "mp4a");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_AAC);
  ck_assert_uint_eq(out.cpriv_len, sizeof asc);
  ck_assert_int_eq(memcmp(out.cpriv, asc, sizeof asc), 0);
}
END_TEST

START_TEST(fmp4_parse_stsd_entry_mp2a_disambiguated_via_esds_oti) {
  trak_meta_t t;
  unsigned char buf[256];
  fmp4_box_t entry;
  fmp4_stsd_entry_t out;

  memset(&t, 0, sizeof t);
  t.track_id = 2;
  t.cls = PID_AUDIO;
  t.codec = CODEC_MP2A;
  t.rate = 48000;
  t.channels = 2;

  build_and_find_entry(&t, buf, sizeof buf, &entry);
  ck_assert_str_eq(entry.fourcc, "mp4a");
  ck_assert_int_eq(fmp4_parse_stsd_entry(&entry, &out), 1);
  ck_assert_int_eq(out.codec, CODEC_MP2A);
}
END_TEST

static Suite *fmp4_track_suite(void) {
  Suite *s = suite_create("fmp4_track");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmp4_parse_stsd_entry_h264_avcc_extracts_sps_pps);
  tcase_add_test(tc, fmp4_parse_stsd_entry_hevc_hvcc_extracts_vps_sps_pps);
  tcase_add_test(tc, fmp4_parse_stsd_entry_vvc_vvcc_extracts_vps_sps_pps);
  tcase_add_test(tc, fmp4_parse_stsd_entry_ac3_extracts_dac3_fields);
  tcase_add_test(tc, fmp4_parse_stsd_entry_eac3_extracts_dec3_fields);
  tcase_add_test(tc, fmp4_parse_stsd_entry_opus_extracts_channels);
  tcase_add_test(tc, fmp4_parse_stsd_entry_dts_hd_ma_extracts_rate);
  tcase_add_test(tc, fmp4_parse_stsd_entry_truehd_extracts_dmlp_fields);
  tcase_add_test(tc, fmp4_parse_stsd_entry_ac4_extracts_opaque_cpriv);
  tcase_add_test(tc, fmp4_parse_stsd_entry_aac_extracts_asc_via_esds);
  tcase_add_test(tc, fmp4_parse_stsd_entry_mp2a_disambiguated_via_esds_oti);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fmp4_track_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
