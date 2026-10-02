/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/fmp4/track.h"
#include "lib/mux/fmp4/box.h"

static void build_and_find_entry(const trak_meta_t *t, unsigned char *buf, size_t bufcap, fmp4_box_t *entry_out) {
  mp4buf_t stsd;
  fmp4_box_t stsd_box;
  fmp4_box_t e;
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

typedef void (*cfg_parser_t)(const unsigned char *p, const unsigned char *end, fmp4_stsd_entry_t *out);
typedef unsigned (*probe_fn_t)(const fmp4_stsd_entry_t *e);

static unsigned char *make_filled(size_t len, unsigned char fill) {
  unsigned char *p = malloc(len ? len : 1);

  ck_assert_ptr_nonnull(p);
  memset(p, fill, len);
  return p;
}

static unsigned char *make_body(size_t prefix, const unsigned char *tail, size_t tail_len, size_t *total) {
  unsigned char *p = make_filled(prefix + tail_len, 0);

  if (tail_len) memcpy(p + prefix, tail, tail_len);
  *total = prefix + tail_len;
  return p;
}

static unsigned probe_bsid(const fmp4_stsd_entry_t *e) {
  return e->ac3_bsid;
}

static unsigned probe_channels(const fmp4_stsd_entry_t *e) {
  return e->channels;
}

static unsigned probe_dts_rate(const fmp4_stsd_entry_t *e) {
  return e->dts_rate;
}

static unsigned probe_truehd_format(const fmp4_stsd_entry_t *e) {
  return e->truehd_format_info;
}

typedef struct {
  const char *name;
  cfg_parser_t fn;
  size_t min_len;
  probe_fn_t probe;
  unsigned expect;
} fixed_case_t;

static const fixed_case_t fixed_cases[] = {
    {"avcC", fmp4_parse_avcc, 6, NULL, 0},
    {"hvcC", fmp4_parse_hvcc, 23, NULL, 0},
    {"vvcC", fmp4_parse_vvcc, 2, NULL, 0},
    {"esds", fmp4_parse_esds, 4, NULL, 0},
    {"dac3", fmp4_parse_dac3, 3, probe_bsid, 0x1F},
    {"dec3", fmp4_parse_dec3, 4, probe_bsid, 0x1F},
    {"dOps", fmp4_parse_dops, 2, probe_channels, 0xFF},
    {"ddts", fmp4_parse_ddts, 4, probe_dts_rate, 0xFFFFFFFFu},
    {"dmlp", fmp4_parse_dmlp, 6, probe_truehd_format, 0xFFFFFFFFu},
};

START_TEST(config_parsers_ignore_bodies_shorter_than_fixed_fields) {
  const fixed_case_t *c = &fixed_cases[_i];
  fmp4_stsd_entry_t zero;
  fmp4_stsd_entry_t out;

  memset(&zero, 0, sizeof zero);
  for (size_t len = 0; len < c->min_len; len++) {
    unsigned char *p = make_filled(len, 0xFF);

    memset(&out, 0, sizeof out);
    c->fn(p, p + len, &out);
    ck_assert_msg(memcmp(&out, &zero, sizeof out) == 0, "%s: body of %zu bytes modified the entry", c->name, len);
    free(p);
  }
}
END_TEST

START_TEST(config_parsers_read_body_of_exactly_fixed_length) {
  const fixed_case_t *c = &fixed_cases[_i];
  unsigned char *p;
  fmp4_stsd_entry_t out;

  if (!c->probe) return;
  p = make_filled(c->min_len, 0xFF);
  memset(&out, 0, sizeof out);
  c->fn(p, p + c->min_len, &out);
  ck_assert_msg(c->probe(&out) == c->expect, "%s: field %u, want %u", c->name, c->probe(&out), c->expect);
  free(p);
}
END_TEST

START_TEST(dac4_takes_whole_body_even_when_empty) {
  static const size_t lens[] = {0, 1, 7};

  for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
    unsigned char *p = make_filled(lens[i], 0xAB);
    fmp4_stsd_entry_t out;

    memset(&out, 0, sizeof out);
    fmp4_parse_dac4(p, p + lens[i], &out);
    ck_assert_ptr_eq(out.cpriv, p);
    ck_assert_uint_eq(out.cpriv_len, lens[i]);
    free(p);
  }
}
END_TEST

typedef struct {
  const char *name;
  cfg_parser_t fn;
  size_t zero_prefix;
  unsigned char tail[40];
  size_t tail_len;
  unsigned n_vps;
  unsigned n_sps;
  unsigned n_pps;
} ps_case_t;

static const ps_case_t ps_cases[] = {
    {"avcC sps length past end", fmp4_parse_avcc, 5, {0xE1, 0x00, 0x10, 0xAA, 0xBB}, 5, 0, 0, 0},
    {"avcC no pps count", fmp4_parse_avcc, 5, {0xE1, 0x00, 0x02, 0xAA, 0xBB}, 5, 0, 1, 0},
    {"avcC pps length past end", fmp4_parse_avcc, 5, {0xE1, 0x00, 0x02, 0xAA, 0xBB, 0x01, 0x00, 0x09, 0xCC}, 9, 0, 1, 0},
    {"avcC sps count past data", fmp4_parse_avcc, 5, {0xE3, 0x00, 0x01, 0xAA}, 4, 0, 1, 0},
    {"avcC more sps than slots", fmp4_parse_avcc, 5,
     {0xE8, 0x00, 0x01, 0x01, 0x00, 0x01, 0x02, 0x00, 0x01, 0x03, 0x00, 0x01, 0x04, 0x00, 0x01, 0x05, 0x00, 0x01, 0x06, 0x00, 0x01, 0x07, 0x00, 0x01, 0x08, 0x00}, 26, 0, FMP4_PS_MAX, 0},
    {"avcC valid", fmp4_parse_avcc, 5, {0xE1, 0x00, 0x02, 0xAA, 0xBB, 0x01, 0x00, 0x01, 0xCC}, 9, 0, 1, 1},
    {"hvcC no arrays present", fmp4_parse_hvcc, 22, {0x02}, 1, 0, 0, 0},
    {"hvcC nal length past end", fmp4_parse_hvcc, 22, {0x01, 0x20, 0x00, 0x01, 0x00, 0x09, 0xAA}, 7, 0, 0, 0},
    {"hvcC vps only", fmp4_parse_hvcc, 22, {0x01, 0x20, 0x00, 0x01, 0x00, 0x02, 0xAA, 0xBB}, 8, 1, 0, 0},
    {"hvcC vps sps pps", fmp4_parse_hvcc, 22,
     {0x03, 0x20, 0x00, 0x01, 0x00, 0x01, 0xAA, 0x21, 0x00, 0x01, 0x00, 0x01, 0xBB, 0x22, 0x00, 0x01, 0x00, 0x01, 0xCC}, 19, 1, 1, 1},
    {"hvcC unknown nal type skipped", fmp4_parse_hvcc, 22, {0x01, 0x27, 0x00, 0x01, 0x00, 0x01, 0xAA}, 7, 0, 0, 0},
    {"hvcC count larger than data", fmp4_parse_hvcc, 22, {0x01, 0x21, 0xFF, 0xFF, 0x00, 0x01, 0xAA}, 7, 0, 1, 0},
    {"hvcC more pps than slots", fmp4_parse_hvcc, 22,
     {0x01, 0x22, 0x00, 0x05, 0x00, 0x01, 0x01, 0x00, 0x01, 0x02, 0x00, 0x01, 0x03, 0x00, 0x01, 0x04, 0x00, 0x01, 0x05}, 19, 0, 0, FMP4_PS_MAX},
    {"vvcC arrays claimed without data", fmp4_parse_vvcc, 0, {0x00, 0x02}, 2, 0, 0, 0},
    {"vvcC nal length past end", fmp4_parse_vvcc, 0, {0x00, 0x01, 0x0E, 0x00, 0x01, 0x00, 0x09, 0xAA}, 8, 0, 0, 0},
    {"vvcC array header cut", fmp4_parse_vvcc, 0, {0x00, 0x01, 0x0E, 0x00, 0x01, 0x00}, 6, 0, 0, 0},
    {"vvcC vps sps pps", fmp4_parse_vvcc, 0,
     {0x00, 0x03, 0x0E, 0x00, 0x01, 0x00, 0x01, 0xAA, 0x0F, 0x00, 0x01, 0x00, 0x01, 0xBB, 0x10, 0x00, 0x01, 0x00, 0x01, 0xCC}, 20, 1, 1, 1},
    {"vvcC unknown nal type skipped", fmp4_parse_vvcc, 0, {0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x01, 0xAA}, 8, 0, 0, 0},
};

START_TEST(parameter_set_parsers_stop_at_truncated_data) {
  const ps_case_t *c = &ps_cases[_i];
  fmp4_stsd_entry_t out;
  size_t total;
  unsigned char *p = make_body(c->zero_prefix, c->tail, c->tail_len, &total);

  memset(&out, 0, sizeof out);
  c->fn(p, p + total, &out);
  ck_assert_msg(out.n_vps == c->n_vps, "%s: n_vps %u, want %u", c->name, out.n_vps, c->n_vps);
  ck_assert_msg(out.n_sps == c->n_sps, "%s: n_sps %u, want %u", c->name, out.n_sps, c->n_sps);
  ck_assert_msg(out.n_pps == c->n_pps, "%s: n_pps %u, want %u", c->name, out.n_pps, c->n_pps);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char tail[40];
  size_t tail_len;
  codec_t codec;
  size_t cpriv_len;
  size_t cpriv_off;
} esds_case_t;

static const esds_case_t esds_cases[] = {
    {"not an ES_Descriptor", {0x04, 0x01, 0x00}, 3, CODEC_NONE, 0, 0},
    {"ES_Descriptor too small", {0x03, 0x02, 0x00, 0x00}, 4, CODEC_NONE, 0, 0},
    {"truncated multibyte length", {0x03, 0x80}, 2, CODEC_NONE, 0, 0},
    {"empty DecoderConfig", {0x03, 0x05, 0x00, 0x00, 0x00, 0x04, 0x00}, 7, CODEC_NONE, 0, 0},
    {"mpeg audio oti with short config", {0x03, 0x0A, 0x00, 0x00, 0x00, 0x04, 0x05, 0x6B, 0x00, 0x00, 0x00, 0x00}, 12, CODEC_MP2A, 0, 0},
    {"DecSpecificInfo past end",
     {0x03, 0x15, 0x00, 0x00, 0x00, 0x04, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x09, 0xAA}, 23, CODEC_NONE, 0, 0},
    {"valid DecSpecificInfo",
     {0x03, 0x16, 0x00, 0x00, 0x00, 0x04, 0x11, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x02, 0x12, 0x10}, 24, CODEC_NONE, 2, 26},
};

START_TEST(esds_parser_rejects_malformed_descriptors) {
  const esds_case_t *c = &esds_cases[_i];
  fmp4_stsd_entry_t out;
  size_t total;
  unsigned char *p = make_body(4, c->tail, c->tail_len, &total);

  memset(&out, 0, sizeof out);
  fmp4_parse_esds(p, p + total, &out);
  ck_assert_msg(out.codec == c->codec, "%s: codec %d", c->name, (int)out.codec);
  ck_assert_msg(out.cpriv_len == c->cpriv_len, "%s: cpriv_len %zu", c->name, out.cpriv_len);
  if (c->cpriv_len) ck_assert_msg(out.cpriv == p + c->cpriv_off, "%s: cpriv offset", c->name);
  else ck_assert_msg(out.cpriv == NULL, "%s: cpriv set", c->name);
  free(p);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char data[8];
  size_t len;
  unsigned tag;
  size_t size;
  size_t body_off;
} esds_desc_case_t;

static const esds_desc_case_t esds_desc_cases[] = {
    {"empty", {0}, 0, 0, 0, 0},
    {"tag without length", {0x05}, 1, 0, 0, 0},
    {"unterminated multibyte length", {0x05, 0x81}, 2, 0, 0, 0},
    {"length past end", {0x05, 0x03, 0xAA}, 3, 0, 0, 0},
    {"runaway length", {0x05, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F}, 6, 0, 0, 0},
    {"zero length", {0x04, 0x00}, 2, 4, 0, 2},
    {"valid", {0x03, 0x02, 0xAA, 0xBB}, 4, 3, 2, 2},
};

START_TEST(esds_descriptor_reader_bounds_checks) {
  const esds_desc_case_t *c = &esds_desc_cases[_i];
  unsigned char *p = make_filled(c->len, 0);
  const unsigned char *cur;
  const unsigned char *body = NULL;
  unsigned tag = 99;
  size_t size = 0;

  if (c->len) memcpy(p, c->data, c->len);
  cur = p;
  fmp4_parse_esds_desc(&cur, p + c->len, &tag, &body, &size);
  ck_assert_msg(tag == c->tag, "%s: tag %u, want %u", c->name, tag, c->tag);
  if (c->tag) {
    ck_assert_msg(size == c->size, "%s: size %zu", c->name, size);
    ck_assert_msg(body == p + c->body_off, "%s: body offset", c->name);
    ck_assert_msg(cur == p + c->body_off + c->size, "%s: cursor", c->name);
  } else {
    ck_assert_msg(cur == p, "%s: cursor moved on failure", c->name);
  }
  free(p);
}
END_TEST

START_TEST(esds_descriptor_reader_decodes_multibyte_length) {
  unsigned char p[3 + 128];
  const unsigned char *cur = p;
  const unsigned char *body = NULL;
  unsigned tag = 0;
  size_t size = 0;

  memset(p, 0x5A, sizeof p);
  p[0] = 0x05;
  p[1] = 0x81;
  p[2] = 0x00;
  fmp4_parse_esds_desc(&cur, p + sizeof p, &tag, &body, &size);
  ck_assert_uint_eq(tag, 5u);
  ck_assert_uint_eq(size, 128u);
  ck_assert_ptr_eq(body, p + 3);
  ck_assert_ptr_eq(cur, p + sizeof p);
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
  tcase_add_loop_test(tc, config_parsers_ignore_bodies_shorter_than_fixed_fields, 0, (int)(sizeof fixed_cases / sizeof fixed_cases[0]));
  tcase_add_loop_test(tc, config_parsers_read_body_of_exactly_fixed_length, 0,(int)(sizeof fixed_cases / sizeof fixed_cases[0]));
  tcase_add_test(tc, dac4_takes_whole_body_even_when_empty);
  tcase_add_loop_test(tc, parameter_set_parsers_stop_at_truncated_data, 0, (int)(sizeof ps_cases / sizeof ps_cases[0]));
  tcase_add_loop_test(tc, esds_parser_rejects_malformed_descriptors, 0, (int)(sizeof esds_cases / sizeof esds_cases[0]));
  tcase_add_loop_test(tc, esds_descriptor_reader_bounds_checks, 0, (int)(sizeof esds_desc_cases / sizeof esds_desc_cases[0]));
  tcase_add_test(tc, esds_descriptor_reader_decodes_multibyte_length);
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
