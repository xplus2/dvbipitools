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

typedef struct {
  const char *name;
  unsigned char d[4];
  size_t len;
  int ret;
  unsigned samples;
  unsigned ch;
} opus_case_t;

static const opus_case_t opus_cases[] = {
    {"empty needs data", {0}, 0, 1, 0, 0},
    {"control header prefix rejected", {0xFF, 0xE0, 0x00, 0x00}, 4, -1, 0, 0},
    {"code0 mono 10ms silk", {0x00, 0xAA, 0, 0}, 2, 0, 480, 1},
    {"code0 stereo celt 20ms", {0x7C, 0xAA, 0, 0}, 2, 0, 960, 2},
    {"code1 two frames", {0x09, 0xAA, 0, 0}, 2, 0, 1920, 1},
    {"code2 two frames stereo", {0x6E, 0xAA, 0, 0}, 2, 0, 960 * 2, 2},
    {"code3 needs count byte", {0x03, 0, 0, 0}, 1, 1, 0, 0},
    {"code3 zero frames rejected", {0x03, 0x00, 0, 0}, 2, -1, 0, 0},
    {"code3 three frames of 20ms", {0x0B, 0x03, 0, 0}, 2, 0, 3 * 960, 1},
    {"code3 over 120ms rejected", {0x1B, 0x03, 0, 0}, 2, -1, 0, 0},
};

START_TEST(opus_toc_cases) {
  const opus_case_t *c = &opus_cases[_i];
  esc_track_t t;
  esc_frame_t f;
  int r;

  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_OPUS;
  r = next_frame(&t, c->d, c->len, &f);
  ck_assert_msg(r == c->ret, "%s: got %d, want %d", c->name, r, c->ret);
  if (r != 0) return;
  ck_assert_msg(f.samples == c->samples, "%s: samples %u want %u", c->name, f.samples, c->samples);
  ck_assert_uint_eq(f.ch, c->ch);
  ck_assert_uint_eq(f.rate, 48000u);
  ck_assert_uint_eq(f.consumed, c->len);
  ck_assert_uint_eq(f.outlen, c->len);
  ck_assert_uint_eq(t.cpriv_len, 19u);
  ck_assert_int_eq(memcmp(t.cpriv, "OpusHead", 8), 0);
  ck_assert_uint_eq(t.cpriv[8], 1u);
  ck_assert_uint_eq(t.cpriv[9], c->ch);
}
END_TEST

START_TEST(opus_head_is_built_once) {
  static const unsigned char first[] = {0x7C, 0xAA};
  static const unsigned char second[] = {0x00, 0xAA};
  esc_track_t t;
  esc_frame_t f;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_OPUS;
  ck_assert_int_eq(next_frame(&t, first, sizeof first, &f), 0);
  ck_assert_uint_eq(t.cpriv[9], 2u);
  ck_assert_int_eq(next_frame(&t, second, sizeof second, &f), 0);
  ck_assert_uint_eq(f.ch, 1u);
  ck_assert_uint_eq(t.cpriv[9], 2u);
}
END_TEST

static void put_variable_bits(bitwriter_t *bw, unsigned value, int n_bits) {
  unsigned lim = 1u << n_bits;

  if (value < lim) {
    bitwriter_put(bw, value, n_bits);
    bitwriter_put(bw, 0, 1);
    return;
  }
  bitwriter_put(bw, (value >> n_bits) - 1, n_bits);
  bitwriter_put(bw, 1, 1);
  bitwriter_put(bw, value & (lim - 1), n_bits);
  bitwriter_put(bw, 0, 1);
}

static size_t finish_ac4_frame(bitwriter_t *bw, unsigned char *out, size_t cap) {
  size_t plen;
  const unsigned char *payload = bitwriter_data(bw, &plen);
  size_t size = plen + 4;

  ck_assert_uint_le(4 + size, cap);
  out[0] = 0xAC;
  out[1] = 0x40;
  out[2] = (unsigned char)(size >> 8);
  out[3] = (unsigned char)size;
  memcpy(out + 4, payload, plen);
  memset(out + 4 + plen, 0, 4);
  bitwriter_free(bw);
  return 4 + size;
}

static void ac4_put_toc_head(bitwriter_t *bw, unsigned version2, unsigned vb_version) {
  bitwriter_put(bw, version2, 2);
  if (version2 == 3) put_variable_bits(bw, vb_version, 2);
  bitwriter_put(bw, 0, 10);
  bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 1, 1);
  bitwriter_put(bw, 3, 4);
  bitwriter_put(bw, 1, 1);
}

static void ac4_put_single_substream_stereo_presentation(bitwriter_t *bw) {
  bitwriter_put(bw, 1, 1);
  bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 0, 3);
  bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 0, 2);
  bitwriter_put(bw, 0, 3);
  bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 0, 2);
  bitwriter_put(bw, 0, 2);
  bitwriter_put(bw, 1, 1);
  bitwriter_put(bw, 0, 1);
}

static const unsigned ac4_variable_values[] = {0, 1, 3, 5, 9};

START_TEST(ac4_bitstream_version_three_adds_variable_bits) {
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 3, ac4_variable_values[_i]);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  ac4_put_single_substream_stereo_presentation(&bw);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.ac4_bitstream_version, 3u + ac4_variable_values[_i]);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(ac4_multiple_presentations_count_uses_variable_bits) {
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 1, 0);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 1, 1);
  put_variable_bits(&bw, ac4_variable_values[_i], 2);
  bitwriter_put(&bw, 0, 1);
  ac4_put_single_substream_stereo_presentation(&bw);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(ac4_payload_base_escape_consumes_variable_bits) {
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 1, 0);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 31, 5);
  put_variable_bits(&bw, ac4_variable_values[_i], 3);
  ac4_put_single_substream_stereo_presentation(&bw);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(ac4_presentation_config_seven_consumes_variable_bits_then_aborts) {
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 1, 0);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 7, 3);
  put_variable_bits(&bw, ac4_variable_values[_i], 2);
  bitwriter_put(&bw, 0, 8);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.ch, 0u);
}
END_TEST

START_TEST(ac4_channel_count_escape_consumes_variable_bits) {
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 1, 0);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 3, 2);
  bitwriter_put(&bw, 7, 3);
  put_variable_bits(&bw, ac4_variable_values[_i], 2);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.ch, 0u);
}
END_TEST

typedef struct {
  const char *name;
  unsigned use_same_mux;
  unsigned amv;
  unsigned amv_a;
  unsigned amv_n;
  unsigned all_same;
  unsigned num_sub;
  unsigned aot;
  unsigned aot_ext;
  unsigned sfi;
  unsigned ch;
  unsigned ext_sfi;
  unsigned inner_aot;
  unsigned depends_core;
  unsigned extension_flag;
  unsigned flt;
  unsigned other_data;
  unsigned crc;
  unsigned plen;
  int want;
  unsigned want_rate;
  unsigned want_ch;
} latm_case_t;

static const latm_case_t latm_cases[] = {
  {"plain", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, 0, 44100, 2},
  {"extended aot", 0, 0, 0, 0, 1, 0, 31, 7, 3, 1, 0, 0, 0, 0, 0, 0, 0, 4, 0, 48000, 1},
  {"sbr with 24-bit extension rate", 0, 0, 0, 0, 1, 0, 5, 0, 6, 2, 15, 2, 0, 0, 0, 0, 0, 4, 0, 24000, 2},
  {"ps with extension rate index", 0, 0, 0, 0, 1, 0, 29, 0, 6, 2, 3, 2, 0, 0, 0, 0, 0, 4, 0, 24000, 2},
  {"audio mux version 1", 0, 1, 0, 1, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, 0, 44100, 2},
  {"audio mux version A set", 0, 1, 1, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
  {"streams not time framed together", 0, 0, 0, 0, 0, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
  {"sub frames present", 0, 0, 0, 0, 1, 1, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
  {"escape sample rate index", 0, 0, 0, 0, 1, 0, 2, 0, 15, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
  {"reserved sample rate index", 0, 0, 0, 0, 1, 0, 2, 0, 13, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
  {"depends on core coder", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 1, 0, 0, 0, 0, 4, 0, 44100, 2},
  {"extension flag set", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 1, 0, 0, 0, 4, -1, 0, 0},
  {"unsupported frame length type", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 1, 0, 0, 4, -1, 0, 0},
  {"other data chain", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 2, 0, 4, 0, 44100, 2},
  {"crc check byte", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 1, 4, 0, 44100, 2},
  {"long payload length escape", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 300, 0, 44100, 2},
  {"empty payload", 0, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0},
  {"channel config zero means stereo", 0, 0, 0, 0, 1, 0, 2, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 44100, 2},
  {"same mux without a config", 1, 0, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 0, 0, 4, -1, 0, 0},
};

static size_t build_latm_case(const latm_case_t *c, unsigned char *out, size_t cap) {
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;
  size_t total;
  unsigned left;

  bitwriter_init(&bw);
  bitwriter_put(&bw, c->use_same_mux, 1);
  if (!c->use_same_mux) {
    bitwriter_put(&bw, c->amv, 1);
    if (c->amv) {
      bitwriter_put(&bw, c->amv_a, 1);
      if (!c->amv_a) {
        bitwriter_put(&bw, c->amv_n, 2);
        for (unsigned i = 0; i <= c->amv_n; i++) bitwriter_put(&bw, 0x11, 8);
      }
    }
    if (!c->amv_a || !c->amv) {
      bitwriter_put(&bw, c->all_same, 1);
      bitwriter_put(&bw, c->num_sub, 6);
      bitwriter_put(&bw, 0, 4);
      bitwriter_put(&bw, 0, 3);
      if (c->aot == 31) {
        bitwriter_put(&bw, 31, 5);
        bitwriter_put(&bw, c->aot_ext, 6);
      } else {
        bitwriter_put(&bw, c->aot, 5);
      }
      bitwriter_put(&bw, c->sfi, 4);
      bitwriter_put(&bw, c->ch, 4);
      if (c->aot == 5 || c->aot == 29) {
        bitwriter_put(&bw, c->ext_sfi, 4);
        if (c->ext_sfi == 15) bitwriter_put(&bw, 0, 24);
        bitwriter_put(&bw, c->inner_aot, 5);
      }
      bitwriter_put(&bw, 0, 1);
      bitwriter_put(&bw, c->depends_core, 1);
      if (c->depends_core) bitwriter_put(&bw, 0, 14);
      bitwriter_put(&bw, c->extension_flag, 1);
      bitwriter_put(&bw, c->flt, 3);
      bitwriter_put(&bw, 0, 8);
      bitwriter_put(&bw, c->other_data ? 1 : 0, 1);
      for (unsigned i = 0; i < c->other_data; i++) {
        bitwriter_put(&bw, i + 1 < c->other_data, 1);
        bitwriter_put(&bw, 0, 8);
      }
      bitwriter_put(&bw, c->crc, 1);
      if (c->crc) bitwriter_put(&bw, 0, 8);
    }
  }
  left = c->plen;
  while (left >= 255) {
    bitwriter_put(&bw, 255, 8);
    left -= 255;
  }
  bitwriter_put(&bw, left, 8);
  for (unsigned i = 0; i < c->plen; i++) bitwriter_put(&bw, 0xA0 + (i & 7), 8);
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

START_TEST(latm_config_and_payload_variants) {
  const latm_case_t *c = &latm_cases[_i];
  unsigned char d[512];
  size_t len = build_latm_case(c, d, sizeof d);
  esc_track_t t;
  esc_frame_t f;
  int r;

  ck_assert_msg(len > 0, "%s: frame too large", c->name);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC_LATM;
  r = next_frame(&t, d, len, &f);
  ck_assert_msg(r == c->want, "%s: ret %d", c->name, r);
  if (!r) {
    ck_assert_msg(f.rate == c->want_rate, "%s: rate %u", c->name, f.rate);
    ck_assert_msg(f.ch == c->want_ch, "%s: ch %u", c->name, f.ch);
    ck_assert_msg(f.outlen == c->plen, "%s: outlen %zu", c->name, f.outlen);
    ck_assert_msg(f.consumed == len, "%s: consumed", c->name);
  }
}
END_TEST

START_TEST(latm_same_mux_reuses_the_cached_config) {
  latm_case_t c = latm_cases[0];
  unsigned char d[64];
  size_t len = build_latm_case(&c, d, sizeof d);
  esc_track_t t;
  esc_frame_t f;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC_LATM;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  c.use_same_mux = 1;
  len = build_latm_case(&c, d, sizeof d);
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_uint_eq(f.rate, 44100u);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(latm_framing_errors) {
  latm_case_t c = latm_cases[0];
  unsigned char d[64];
  size_t len = build_latm_case(&c, d, sizeof d);
  esc_track_t t;
  esc_frame_t f;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC_LATM;
  ck_assert_int_eq(next_frame(&t, d, 2, &f), 1);
  ck_assert_int_eq(next_frame(&t, d, len - 1, &f), 1);
  d[0] = 0x55;
  ck_assert_int_eq(next_frame(&t, d, len, &f), -1);
  d[0] = 0x56;
  d[1] &= 0x1F;
  ck_assert_int_eq(next_frame(&t, d, len, &f), -1);
}
END_TEST

START_TEST(adts_with_crc_header_and_codec_private_set_once) {
  unsigned char d[128] = {0xFF, 0xF0, 0x50, 0x80, 0x10, 0x00, 0x00};
  unsigned char other[128] = {0xFF, 0xF1, 0x0C, 0xC0, 0x10, 0x00, 0x00};
  esc_track_t t;
  esc_frame_t f;

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC;
  ck_assert_int_eq(next_frame(&t, d, sizeof d, &f), 0);
  ck_assert_uint_eq(f.consumed, 128u);
  ck_assert_ptr_eq(f.out, d + 9);
  ck_assert_uint_eq(f.outlen, 119u);
  ck_assert_uint_eq(f.rate, 44100u);
  ck_assert_uint_eq(f.ch, 2u);
  ck_assert_uint_eq(t.cpriv_len, 2u);
  ck_assert_uint_eq(t.cpriv[0], 0x12u);
  ck_assert_uint_eq(t.cpriv[1], 0x10u);
  ck_assert_int_eq(next_frame(&t, other, sizeof other, &f), 0);
  ck_assert_ptr_eq(f.out, other + 7);
  ck_assert_uint_eq(t.cpriv[0], 0x12u);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char b1;
  unsigned ext_size;
  unsigned size_field;
  size_t trim;
  int want;
  size_t want_consumed;
} ac4_hdr_case_t;

static const ac4_hdr_case_t ac4_hdr_cases[] = {
  {"crc frame carries two trailer bytes", 0x41, 0, 0, 0, 0, 0},
  {"crc frame truncated before trailer", 0x41, 0, 0, 1, 1, 0},
  {"extended size header", 0x40, 1, 0, 0, 0, 0},
  {"extended size header truncated", 0x40, 1, 0, 100, 1, 0},
  {"zero frame size", 0x40, 0, 1, 0, -1, 0},
  {"bad second sync byte", 0x42, 0, 0, 0, -1, 0},
};

START_TEST(ac4_frame_header_variants) {
  const ac4_hdr_case_t *c = &ac4_hdr_cases[_i];
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;
  unsigned char d[96];
  size_t hdr = c->ext_size ? 7 : 4;
  size_t total;
  size_t len;
  esc_track_t t;
  esc_frame_t f;
  int r;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 0, 0);
  ac4_put_single_substream_stereo_presentation(&bw);
  payload = bitwriter_data(&bw, &plen);
  plen += 4;
  memset(d, 0, sizeof d);
  d[0] = 0xAC;
  d[1] = c->b1;
  if (c->ext_size) {
    d[2] = 0xFF;
    d[3] = 0xFF;
    d[4] = 0;
    d[5] = (unsigned char)(plen >> 8);
    d[6] = (unsigned char)plen;
  } else if (c->size_field) {
    d[2] = 0;
    d[3] = 0;
  } else {
    d[2] = (unsigned char)(plen >> 8);
    d[3] = (unsigned char)plen;
  }
  memcpy(d + hdr, payload, plen - 4);
  total = hdr + plen + (c->b1 == 0x41 ? 2u : 0u);
  len = total > c->trim ? total - c->trim : 0;
  if (c->ext_size && c->trim) len = 5;
  bitwriter_free(&bw);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, len, &f);
  ck_assert_msg(r == c->want, "%s: ret %d", c->name, r);
  if (!r) ck_assert_msg(f.consumed == total, "%s: consumed %zu want %zu", c->name, f.consumed, total);
}
END_TEST

typedef struct {
  const char *name;
  unsigned fs;
  unsigned frame_rate;
  unsigned wait_flag;
  unsigned wait_frames;
  int want;
  unsigned want_rate;
  unsigned want_samples;
} ac4_rate_case_t;

static const ac4_rate_case_t ac4_rate_cases[] = {
  {"44.1 kHz needs frame rate index 13", 0, 13, 0, 0, 0, 44100, 2048},
  {"44.1 kHz with another frame rate", 0, 5, 0, 0, -1, 0, 0},
  {"frame rate index 14 is reserved", 1, 14, 0, 0, -1, 0, 0},
  {"frame rate index 15 is reserved", 1, 15, 0, 0, -1, 0, 0},
  {"48 kHz 23.97 fps", 1, 0, 0, 0, 0, 48000, 1920},
  {"wait frames present", 1, 3, 1, 2, 0, 48000, 1536},
  {"wait frames flag with zero count", 1, 3, 1, 0, 0, 48000, 1536},
};

START_TEST(ac4_sample_rate_frame_rate_and_wait_frames) {
  const ac4_rate_case_t *c = &ac4_rate_cases[_i];
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  int r;

  bitwriter_init(&bw);
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 0, 10);
  bitwriter_put(&bw, c->wait_flag, 1);
  if (c->wait_flag) {
    bitwriter_put(&bw, c->wait_frames, 3);
    if (c->wait_frames > 0) bitwriter_put(&bw, 0, 2);
  }
  bitwriter_put(&bw, c->fs, 1);
  bitwriter_put(&bw, c->frame_rate, 4);
  bitwriter_put(&bw, 1, 1);
  payload = bitwriter_data(&bw, &plen);
  ck_assert_uint_le(plen + 8, sizeof d);
  memset(d, 0, sizeof d);
  d[0] = 0xAC;
  d[1] = 0x40;
  d[2] = 0;
  d[3] = (unsigned char)(plen + 4);
  memcpy(d + 4, payload, plen);
  bitwriter_free(&bw);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, plen + 8, &f);
  ck_assert_msg(r == c->want, "%s: ret %d", c->name, r);
  if (!r) {
    ck_assert_msg(f.rate == c->want_rate, "%s: rate %u", c->name, f.rate);
    ck_assert_msg(f.samples == c->want_samples, "%s: samples %u", c->name, f.samples);
    ck_assert_msg(f.ac4_iframe == 1, "%s: iframe", c->name);
  }
}
END_TEST

typedef struct {
  const char *name;
  unsigned single;
  unsigned config;
  unsigned pver;
  unsigned frame_rate;
  unsigned rate_bit_a;
  unsigned rate_bit_b;
  unsigned emdf_version;
  unsigned key_id;
  unsigned sub_flag;
  unsigned sub_idx;
  unsigned lp;
  unsigned ls;
  unsigned ch_code;
  unsigned want_ch;
} ac4_pres_case_t;

static const ac4_pres_case_t ac4_pres_cases[] = {
  {"mono", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
  {"stereo", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2},
  {"three channels", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3},
  {"five channels", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 3, 5},
  {"six channels", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 4, 6},
  {"seven channels", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 5, 7},
  {"eight channels", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 6, 8},
  {"reserved channel mode", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0},
  {"channel escape", 1, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0},
  {"emdf with every optional field", 1, 0, 2, 3, 0, 0, 3, 7, 1, 3, 2, 3, 1, 2},
  {"frame rate multiply for 24 fps family", 1, 0, 0, 2, 1, 1, 0, 0, 0, 0, 0, 0, 1, 2},
  {"frame rate multiply for 23.97 fps", 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 2},
  {"multi substream presentation", 0, 2, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2},
  {"presentation config 6 aborts", 0, 6, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
  {"presentation config above 5 aborts", 0, 7, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
};

START_TEST(ac4_presentation_parsing_variants) {
  const ac4_pres_case_t *c = &ac4_pres_cases[_i];
  bitwriter_t bw;
  const unsigned char *payload;
  size_t plen;
  unsigned char d[160];
  esc_track_t t;
  esc_frame_t f;
  unsigned n_skip = 0;
  int r;

  bitwriter_init(&bw);
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 0, 10);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, c->frame_rate, 4);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, c->single, 1);
  if (!c->single) {
    bitwriter_put(&bw, c->config, 3);
    if (c->config == 7) put_variable_bits(&bw, 0, 2);
  }
  for (unsigned i = 0; i < c->pver; i++) bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  if (c->single || c->config != 6) {
    bitwriter_put(&bw, 5, 3);
    bitwriter_put(&bw, 1, 1);
    put_variable_bits(&bw, 1, 2);
    if (c->frame_rate >= 2 && c->frame_rate <= 4) {
      bitwriter_put(&bw, c->rate_bit_a, 1);
      if (c->rate_bit_a) bitwriter_put(&bw, c->rate_bit_b, 1);
    } else if (c->frame_rate <= 1 || (c->frame_rate >= 7 && c->frame_rate <= 9)) {
      bitwriter_put(&bw, c->rate_bit_a, 1);
    }
    bitwriter_put(&bw, c->emdf_version, 2);
    if (c->emdf_version == 3) put_variable_bits(&bw, 0, 2);
    bitwriter_put(&bw, c->key_id, 3);
    if (c->key_id == 7) put_variable_bits(&bw, 0, 3);
    bitwriter_put(&bw, c->sub_flag, 1);
    if (c->sub_flag) {
      bitwriter_put(&bw, c->sub_idx, 2);
      if (c->sub_idx == 3) put_variable_bits(&bw, 0, 2);
    }
    bitwriter_put(&bw, c->lp, 2);
    bitwriter_put(&bw, c->ls, 2);
    if (c->lp) n_skip += 1u << (2 * (c->lp - 1));
    if (c->ls) n_skip += 1u << (2 * (c->ls - 1));
    for (unsigned i = 0; i < n_skip; i++) bitwriter_put(&bw, 0, 8);
    if (!c->single) bitwriter_put(&bw, 0, 1);
    if (c->ch_code == 0) {
      bitwriter_put(&bw, 0, 1);
    } else if (c->ch_code == 1) {
      bitwriter_put(&bw, 1, 1);
      bitwriter_put(&bw, 0, 1);
    } else if (c->ch_code <= 4) {
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, c->ch_code - 2, 2);
    } else if (c->ch_code <= 6) {
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, c->ch_code - 5, 3);
    } else if (c->ch_code == 7) {
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, 6, 3);
    } else {
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, 3, 2);
      bitwriter_put(&bw, 7, 3);
      put_variable_bits(&bw, 0, 2);
    }
  }
  payload = bitwriter_data(&bw, &plen);
  ck_assert_uint_le(plen + 8, sizeof d);
  memset(d, 0, sizeof d);
  d[0] = 0xAC;
  d[1] = 0x40;
  d[2] = 0;
  d[3] = (unsigned char)(plen + 4);
  memcpy(d + 4, payload, plen);
  bitwriter_free(&bw);
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_AC4;
  r = next_frame(&t, d, plen + 8, &f);
  ck_assert_msg(r == 0, "%s: ret %d", c->name, r);
  ck_assert_msg(f.ch == c->want_ch, "%s: ch %u", c->name, f.ch);
  if (c->want_ch) ck_assert_msg(f.ac4_presentation_version == c->pver, "%s: pver %u", c->name, f.ac4_presentation_version);
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
  tcase_add_loop_test(tc, opus_toc_cases, 0, (int)(sizeof opus_cases / sizeof opus_cases[0]));
  tcase_add_test(tc, opus_head_is_built_once);
  tcase_add_loop_test(tc, ac4_bitstream_version_three_adds_variable_bits, 0, (int)(sizeof ac4_variable_values / sizeof ac4_variable_values[0]));
  tcase_add_loop_test(tc, ac4_multiple_presentations_count_uses_variable_bits, 0, (int)(sizeof ac4_variable_values / sizeof ac4_variable_values[0]));
  tcase_add_loop_test(tc, ac4_payload_base_escape_consumes_variable_bits, 0, (int)(sizeof ac4_variable_values / sizeof ac4_variable_values[0]));
  tcase_add_loop_test(tc, ac4_presentation_config_seven_consumes_variable_bits_then_aborts, 0, (int)(sizeof ac4_variable_values / sizeof ac4_variable_values[0]));
  tcase_add_loop_test(tc, ac4_channel_count_escape_consumes_variable_bits, 0, (int)(sizeof ac4_variable_values / sizeof ac4_variable_values[0]));
  tcase_add_loop_test(tc, audio_frame_edge_cases, 0, (int)(sizeof frame_cases / sizeof frame_cases[0]));
  tcase_add_loop_test(tc, latm_config_and_payload_variants, 0, (int)(sizeof latm_cases / sizeof latm_cases[0]));
  tcase_add_test(tc, latm_same_mux_reuses_the_cached_config);
  tcase_add_test(tc, latm_framing_errors);
  tcase_add_test(tc, adts_with_crc_header_and_codec_private_set_once);
  tcase_add_loop_test(tc, ac4_frame_header_variants, 0, (int)(sizeof ac4_hdr_cases / sizeof ac4_hdr_cases[0]));
  tcase_add_loop_test(tc, ac4_sample_rate_frame_rate_and_wait_frames, 0, (int)(sizeof ac4_rate_cases / sizeof ac4_rate_cases[0]));
  tcase_add_loop_test(tc, ac4_presentation_parsing_variants, 0, (int)(sizeof ac4_pres_cases / sizeof ac4_pres_cases[0]));
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
