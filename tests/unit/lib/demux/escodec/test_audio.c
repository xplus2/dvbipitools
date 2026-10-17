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
    {"toc 0xFF with 32 frames rejected", {0xFF, 0xE0, 0x00, 0x00}, 4, -1, 0, 0},
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

START_TEST(opus_ts_control_header_is_stripped) {
  static const struct {
    const char *name;
    unsigned char d[300];
    size_t len;
    int ret;
    size_t hdr;
    size_t au;
  } cases[] = {
    {"plain size", {0x7F, 0xE0, 0x02, 0x7C, 0xAA}, 5, 0, 3, 2},
    {"size extended by 0xFF", {0x7F, 0xE0, 0xFF, 0x03, 0x7C, 0xAA}, 6, 1, 0, 0},
    {"start trim", {0x7F, 0xF0, 0x02, 0x00, 0x10, 0x7C, 0xAA}, 7, 0, 5, 2},
    {"start and end trim", {0x7F, 0xF8, 0x02, 0x00, 0x10, 0x00, 0x20, 0x7C, 0xAA}, 9, 0, 7, 2},
    {"extension", {0x7F, 0xE4, 0x02, 0x02, 0x11, 0x22, 0x7C, 0xAA}, 8, 0, 6, 2},
    {"truncated payload", {0x7F, 0xE0, 0x05, 0x7C, 0xAA}, 5, 1, 0, 0},
    {"truncated size", {0x7F, 0xE0}, 2, 1, 0, 0},
    {"zero size", {0x7F, 0xE0, 0x00, 0x7C}, 4, -1, 0, 0},
    {"followed by a second au", {0x7F, 0xE0, 0x02, 0x7C, 0xAA, 0x7F, 0xE0, 0x02, 0x7C, 0xBB}, 10, 0, 3, 2},
  };
  esc_track_t t;
  esc_frame_t f;
  int r;
  memset(&t, 0, sizeof t);
  memset(&f, 0, sizeof f);
  t.codec = CODEC_OPUS;
  r = next_frame(&t, cases[_i].d, cases[_i].len, &f);
  ck_assert_msg(r == cases[_i].ret, "%s: got %d, want %d", cases[_i].name, r, cases[_i].ret);
  if (r != 0) return;
  ck_assert_msg(f.consumed == cases[_i].hdr + cases[_i].au, "%s: consumed %zu", cases[_i].name, f.consumed);
  ck_assert_msg(f.outlen == cases[_i].au, "%s: outlen %zu", cases[_i].name, f.outlen);
  ck_assert_ptr_eq(f.out, cases[_i].d + cases[_i].hdr);
  ck_assert_uint_eq(f.ch, 2u);
  ck_assert_uint_eq(f.samples, 960u);
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

/* bitstream_version 2 TOC tail: one presentation, one substream group, frame_rate_index 3, fs_index 1 */
static void ac4_put_v1_presentation(bitwriter_t *bw, unsigned mode_code, int mode_bits, unsigned flags, int flags_bits, int channel_coded, int add_ch_base) {
  bitwriter_put(bw, 0, 1);  /* b_program_id */
  bitwriter_put(bw, 1, 1);  /* b_single_substream_group */
  bitwriter_put(bw, 0, 1);  /* presentation_version() */
  bitwriter_put(bw, 0, 3);  /* md_compat */
  bitwriter_put(bw, 0, 1);  /* b_presentation_id */
  bitwriter_put(bw, 0, 1);  /* b_multiplier */
  bitwriter_put(bw, 0, 10); /* emdf_info without payloads or protection bytes */
  bitwriter_put(bw, 0, 1);  /* b_presentation_filter */
  bitwriter_put(bw, 0, 3);  /* group_index */
  bitwriter_put(bw, 0, 1);  /* b_pre_virtualized */
  bitwriter_put(bw, 0, 1);  /* b_add_emdf_substreams */
  bitwriter_put(bw, 0, 4);  /* ac4_presentation_substream_info() */
  bitwriter_put(bw, 1, 1);  /* b_substreams_present */
  bitwriter_put(bw, 0, 1);  /* b_hsf_ext */
  bitwriter_put(bw, 1, 1);  /* b_single_substream */
  bitwriter_put(bw, (unsigned)channel_coded, 1);
  if (!channel_coded) return;
  bitwriter_put(bw, mode_code, mode_bits);
  if (flags_bits) bitwriter_put(bw, flags, flags_bits);
  bitwriter_put(bw, 0, 1);  /* b_sf_multiplier */
  bitwriter_put(bw, 0, 1);  /* b_bitrate_info */
  if (add_ch_base) bitwriter_put(bw, 0, 1);
  bitwriter_put(bw, 0, 1);  /* b_audio_ndot */
  bitwriter_put(bw, 0, 2);  /* substream_index */
  bitwriter_put(bw, 0, 1);  /* b_content_type */
}

static size_t ac4_v2_frame(unsigned char *d, size_t cap, unsigned mode_code, int mode_bits, unsigned flags, int flags_bits, int channel_coded, int add_ch_base) {
  bitwriter_t bw;
  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 2, 0);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  ac4_put_v1_presentation(&bw, mode_code, mode_bits, flags, flags_bits, channel_coded, add_ch_base);
  return finish_ac4_frame(&bw, d, cap);
}

typedef struct {
  const char *name;
  unsigned mode_code;
  int mode_bits;
  unsigned flags;
  int flags_bits;
  int add_ch_base;
  unsigned value;
  unsigned ch;
} ac4_v2_case_t;

/* flags: b_4_back_channels_present, b_centre_present, top_channels_present (2 bits). values: TS 103 190-2 table A.27, Dolby examples */
static const ac4_v2_case_t ac4_v2_cases[] = {
  {"mono", 0x0, 1, 0, 0, 0, 0x000002, 1},
  {"stereo", 0x2, 2, 0, 0, 0, 0x000001, 2},
  {"3.0", 0xC, 4, 0, 0, 0, 0x000003, 3},
  {"5.0", 0xD, 4, 0, 0, 0, 0x000007, 5},
  {"5.1", 0xE, 4, 0, 0, 0, 0x000047, 6},
  {"7.0 3/4/0", 0x78, 7, 0, 0, 0, 0x00000F, 7},
  {"7.1 3/4/0.1", 0x79, 7, 0, 0, 0, 0x00004F, 8},
  {"7.0 5/2/0", 0x7A, 7, 0, 0, 1, 0x020007, 7},
  {"7.1 5/2/0.1", 0x7B, 7, 0, 0, 1, 0x020047, 8},
  {"7.0 3/2/2", 0x7C, 7, 0, 0, 1, 0x000017, 7},
  {"7.1 3/2/2.1", 0x7D, 7, 0, 0, 1, 0x000057, 8},
  {"5.1.2", 0xFD, 8, 0x5, 4, 0, 0x0000C7, 8},
  {"5.1.4", 0xFD, 8, 0x7, 4, 0, 0x000077, 10},
  {"7.1.2", 0xFD, 8, 0xD, 4, 0, 0x0000CF, 10},
  {"7.1.4", 0xFD, 8, 0xF, 4, 0, 0x00007F, 12},
  {"7.0.4", 0xFC, 8, 0xF, 4, 0, 0x00003F, 11},
};

static const unsigned ac4_variable_values[] = {0, 1, 3, 5, 9};

START_TEST(ac4_v2_channel_modes_map_to_dolby_2015_speaker_groups) {
  const ac4_v2_case_t *c = &ac4_v2_cases[_i];
  unsigned char d[96];
  esc_track_t t;
  esc_frame_t f;
  size_t len = ac4_v2_frame(d, sizeof d, c->mode_code, c->mode_bits, c->flags, c->flags_bits, 1, c->add_ch_base);

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_msg(f.chcfg_scheme == ESC_CHCFG_DOLBY_2015, "%s scheme %d", c->name, (int)f.chcfg_scheme);
  ck_assert_msg(f.chcfg_value == c->value, "%s value %06X want %06X", c->name, f.chcfg_value, c->value);
  ck_assert_msg(f.ch == c->ch, "%s channels %u want %u", c->name, f.ch, c->ch);
}
END_TEST

START_TEST(ac4_v2_object_based_group_is_flagged_in_bit_23) {
  unsigned char d[96];
  esc_track_t t;
  esc_frame_t f;
  size_t len = ac4_v2_frame(d, sizeof d, 0, 0, 0, 0, 0, 0);

  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2015);
  ck_assert_uint_eq(f.chcfg_value, 0x800000u);
}
END_TEST

START_TEST(ac4_v0_streams_use_the_dolby_2011_location_masks) {
  static const struct {
    unsigned code;
    int bits;
    unsigned value;
  } cases[] = {{0x0, 1, 0x4000}, {0x2, 2, 0xA000}, {0xD, 4, 0xF800}, {0xE, 4, 0xF802}, {0x78, 7, 0xFA00}, {0x79, 7, 0xFA02}, {0x7A, 7, 0xF820}, {0x7B, 7, 0xF822}, {0x7C, 7, 0xF810}, {0x7D, 7, 0xF812}};
  bitwriter_t bw;
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  size_t len;

  bitwriter_init(&bw);
  ac4_put_toc_head(&bw, 0, 0);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 1, 1);  /* b_single_substream */
  bitwriter_put(&bw, 0, 1);  /* presentation_version() */
  bitwriter_put(&bw, 0, 3);  /* md_compat */
  bitwriter_put(&bw, 0, 1);  /* b_presentation_id */
  bitwriter_put(&bw, 0, 1);  /* b_multiplier */
  bitwriter_put(&bw, 0, 10); /* emdf_info */
  bitwriter_put(&bw, cases[_i].code, cases[_i].bits);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2011);
  ck_assert_uint_eq(f.chcfg_value, cases[_i].value);
}
END_TEST

START_TEST(ac4_v1_streams_with_a_v0_presentation_use_speaker_groups) {
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
  bitwriter_put(&bw, 0, 10);
  bitwriter_put(&bw, 0xE, 4);
  len = finish_ac4_frame(&bw, d, sizeof d);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2015);
  ck_assert_uint_eq(f.chcfg_value, 0x47u);
}
END_TEST

START_TEST(ac3_and_eac3_report_dolby_2011_channel_locations) {
  static const struct {
    unsigned acmod;
    unsigned lfe;
    unsigned value;
  } cases[] = {{0, 0, 0xA000}, {1, 0, 0x4000}, {2, 0, 0xA000}, {3, 0, 0xE000}, {4, 0, 0xA100}, {5, 1, 0xE101}, {6, 0, 0xB800}, {7, 1, 0xF801}, {7, 0, 0xF800}};
  unsigned char d[128];
  esc_track_t t;
  esc_frame_t f;

  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    memset(&t, 0, sizeof t);
    memset(d, 0, sizeof d);
    d[0] = 0x0B;
    d[1] = 0x77;
    d[5] = 8 << 3;
    d[6] = (unsigned char)(cases[i].acmod << 5);
    {
      unsigned nbits = 3; /* acmod, then optional cmixlev, surmixlev, dsurmod, then lfeon */
      if ((cases[i].acmod & 1) && cases[i].acmod != 1) nbits += 2;
      if (cases[i].acmod & 4) nbits += 2;
      if (cases[i].acmod == 2) nbits += 2;
      d[6] |= (unsigned char)(cases[i].lfe << (7 - nbits));
    }
    t.codec = CODEC_AC3;
    ck_assert_int_eq(next_frame(&t, d, sizeof d, &f), 0);
    ck_assert_uint_eq(f.acmod, cases[i].acmod);
    ck_assert_uint_eq(f.lfeon, cases[i].lfe);
    ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2011);
    ck_assert_msg(f.chcfg_value == cases[i].value, "ac3 acmod %u lfe %u: %04X", cases[i].acmod, cases[i].lfe, f.chcfg_value);

    memset(d, 0, sizeof d);
    d[0] = 0x0B;
    d[1] = 0x77;
    d[3] = 63;
    d[4] = (unsigned char)(0x30 | (cases[i].acmod << 1) | cases[i].lfe);
    d[5] = 16 << 3;
    t.codec = CODEC_EAC3;
    ck_assert_int_eq(next_frame(&t, d, sizeof d, &f), 0);
    ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2011);
    ck_assert_msg(f.chcfg_value == cases[i].value, "eac3 acmod %u lfe %u: %04X", cases[i].acmod, cases[i].lfe, f.chcfg_value);
  }
}
END_TEST

static size_t dts_core_frame(unsigned char *d, size_t cap, unsigned amode, unsigned lff) {
  bitwriter_t bw;
  const unsigned char *p;
  size_t plen;
  bitwriter_init(&bw);
  bitwriter_put(&bw, 0x7FFE8001u, 32);
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 31, 5);
  bitwriter_put(&bw, 0, 1);
  bitwriter_put(&bw, 15, 7);
  bitwriter_put(&bw, 96, 14);
  bitwriter_put(&bw, amode, 6);
  bitwriter_put(&bw, 13, 4);
  bitwriter_put(&bw, 0, 15);
  bitwriter_put(&bw, lff, 2);
  p = bitwriter_data(&bw, &plen);
  ck_assert_uint_le(97, cap);
  memset(d, 0, 97);
  memcpy(d, p, plen);
  bitwriter_free(&bw);
  return 97;
}

START_TEST(dts_core_reports_total_channels_including_lfe) {
  static const struct {
    unsigned amode;
    unsigned lff;
    unsigned total;
  } cases[] = {{0, 0, 1}, {2, 0, 2}, {9, 0, 5}, {9, 1, 6}, {9, 2, 6}, {12, 1, 7}, {15, 1, 9}};
  unsigned char d[128];
  esc_track_t t;
  esc_frame_t f;
  size_t len = dts_core_frame(d, sizeof d, cases[_i].amode, cases[_i].lff);

  memset(&t, 0, sizeof t);
  t.codec = CODEC_DTS;
  ck_assert_int_eq(next_frame(&t, d, len, &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DTS_2012);
  ck_assert_uint_eq(f.chcfg_value, cases[_i].total);
}
END_TEST

START_TEST(dts_extension_asset_overrides_the_core_channel_count) {
  bitwriter_t bw;
  const unsigned char *p;
  size_t plen;
  unsigned char d[160];
  esc_track_t t;
  esc_frame_t f;
  size_t core = dts_core_frame(d, sizeof d, 9, 1);

  bitwriter_init(&bw);
  bitwriter_put(&bw, 0x64582025u, 32);
  bitwriter_put(&bw, 0, 8);
  bitwriter_put(&bw, 0, 2);  /* nExtSSIndex */
  bitwriter_put(&bw, 0, 1);  /* short header */
  bitwriter_put(&bw, 20, 8);
  bitwriter_put(&bw, 39, 16);
  bitwriter_put(&bw, 1, 1);  /* bStaticFieldsPresent */
  bitwriter_put(&bw, 0, 2);
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 0, 1);  /* bTimeStampFlag */
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 0, 3);
  bitwriter_put(&bw, 1, 1);  /* nuActiveExSSMask[0] */
  bitwriter_put(&bw, 1, 8);  /* nuActiveAssetMask */
  bitwriter_put(&bw, 0, 1);  /* bMixMetadataEnbl */
  bitwriter_put(&bw, 20, 16);
  bitwriter_put(&bw, 10, 9);
  bitwriter_put(&bw, 0, 3);   /* nuAssetIndex */
  bitwriter_put(&bw, 0, 1);   /* bAssetTypeDescrPresent */
  bitwriter_put(&bw, 0, 1);   /* bLanguageDescrPresent */
  bitwriter_put(&bw, 0, 1);   /* bInfoTextPresent */
  bitwriter_put(&bw, 23, 5);
  bitwriter_put(&bw, 9, 4);   /* nuMaxSampleRate */
  bitwriter_put(&bw, 7, 8);
  p = bitwriter_data(&bw, &plen);
  ck_assert_uint_le(core + 40, sizeof d);
  memset(d + core, 0, 40);
  memcpy(d + core, p, plen);
  bitwriter_free(&bw);

  memset(&t, 0, sizeof t);
  t.codec = CODEC_DTS_HD_MA;
  ck_assert_int_eq(next_frame(&t, d, core + 40, &f), 0);
  ck_assert_uint_eq(f.consumed, core + 40);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DTS_2012);
  ck_assert_uint_eq(f.chcfg_value, 8u);
}
END_TEST

START_TEST(truehd_arrangements_map_to_cicp_when_a_layout_exists) {
  static const struct {
    unsigned chanmap;
    unsigned cicp;
  } cases[] = {{0x1, 2}, {0x3, 3}, {0xF, 6}, {0x2F, 12}, {0x2, 1}, {0x13, 0}, {0x4, 0}};
  unsigned char d[22] = {0x00, 0x0B, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA, 0x00, 0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x40, 0x81};
  esc_track_t t;
  esc_frame_t f;

  d[10] = (unsigned char)(cases[_i].chanmap >> 8);
  d[11] = (unsigned char)cases[_i].chanmap;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_TRUEHD;
  ck_assert_int_eq(next_frame(&t, d, sizeof d, &f), 0);
  if (cases[_i].cicp) {
    ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_CICP);
    ck_assert_uint_eq(f.chcfg_value, cases[_i].cicp);
  } else {
    ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_NONE);
  }
}
END_TEST

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
  ac4_put_v1_presentation(&bw, 0x2, 2, 0, 0, 1, 0);
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
  {"audio mux version 1 sbr", 0, 1, 0, 1, 1, 0, 5, 0, 6, 2, 3, 2, 0, 0, 0, 0, 0, 4, 0, 24000, 2},
  {"audio mux version 1 other data", 0, 1, 0, 0, 1, 0, 2, 0, 4, 2, 0, 0, 0, 0, 0, 2, 0, 4, 0, 44100, 2},
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
      if (c->amv) {
        unsigned asc_bits = (c->aot == 31 ? 11u : 5u) + 4 + 4 + 1 + 1 + (c->depends_core ? 14u : 0u) + 1;
        if (c->aot == 5 || c->aot == 29) asc_bits += 4 + (c->ext_sfi == 15 ? 24u : 0u) + 5;
        bitwriter_put(&bw, 0, 2);
        bitwriter_put(&bw, asc_bits, 8);
      }
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
      if (c->amv && c->other_data) {
        bitwriter_put(&bw, 0, 2);
        bitwriter_put(&bw, c->other_data * 8, 8);
      }
      for (unsigned i = 0; i < c->other_data && !c->amv; i++) {
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

START_TEST(ac3_bit_rate_code_is_half_frmsizecod) {
  unsigned char buf[FRAME_BUF_MAX];
  esc_track_t t;
  esc_frame_t f;

  memset(buf, 0, sizeof buf);
  buf[0] = 0x0B;
  buf[1] = 0x77;
  buf[4] = 0x0A; /* fscod 0, frmsizecod 10 */
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC3;
  ck_assert_int_eq(next_frame(&t, buf, sizeof buf, &f), 0);
  ck_assert_uint_eq(f.bitrate_code, 5u);
}
END_TEST

START_TEST(eac3_dependent_substream_joins_the_access_unit) {
  unsigned char buf[FRAME_BUF_MAX];
  esc_track_t t;
  esc_frame_t f;

  memset(buf, 0, sizeof buf);
  buf[0] = 0x0B;
  buf[1] = 0x77;
  buf[3] = 0x3F; /* indie, 128 bytes */
  buf[4] = 0x30; /* fscod 0, numblkscod 3 */
  buf[128] = 0x0B;
  buf[129] = 0x77;
  buf[130] = 0x40; /* strmtyp 1 */
  buf[131] = 0x1F; /* 64 bytes */
  memset(&t, 0, sizeof t);
  t.codec = CODEC_EAC3;
  ck_assert_int_eq(next_frame(&t, buf, 192, &f), 0);
  ck_assert_uint_eq(f.consumed, 192u);
  ck_assert_uint_eq(f.outlen, 192u);
  ck_assert_uint_eq(f.samples, 1536u);
  ck_assert_int_eq(next_frame(&t, buf, 128 + 40, &f), 1);
  ck_assert_int_eq(next_frame(&t, buf + 128, 64, &f), -1);
}
END_TEST

START_TEST(adts_counts_raw_data_blocks_per_frame) {
  unsigned char d[128];
  esc_track_t t;
  esc_frame_t f;

  memset(d, 0, sizeof d);
  d[0] = 0xFF;
  d[1] = 0xF1;
  d[2] = 0x50;
  d[3] = 0x80;
  d[4] = 0x10; /* 128 bytes */
  d[5] = 0x00;
  d[6] = 0x01;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AAC;
  ck_assert_int_eq(next_frame(&t, d, sizeof d, &f), 0);
  ck_assert_uint_eq(f.samples, 2048u);
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
  tcase_add_loop_test(tc, ac4_v2_channel_modes_map_to_dolby_2015_speaker_groups, 0, (int)(sizeof ac4_v2_cases / sizeof ac4_v2_cases[0]));
  tcase_add_test(tc, ac4_v2_object_based_group_is_flagged_in_bit_23);
  tcase_add_loop_test(tc, ac4_v0_streams_use_the_dolby_2011_location_masks, 0, 10);
  tcase_add_test(tc, ac4_v1_streams_with_a_v0_presentation_use_speaker_groups);
  tcase_add_test(tc, ac3_and_eac3_report_dolby_2011_channel_locations);
  tcase_add_loop_test(tc, dts_core_reports_total_channels_including_lfe, 0, 7);
  tcase_add_test(tc, dts_extension_asset_overrides_the_core_channel_count);
  tcase_add_loop_test(tc, truehd_arrangements_map_to_cicp_when_a_layout_exists, 0, 7);
  tcase_add_loop_test(tc, opus_toc_cases, 0, (int)(sizeof opus_cases / sizeof opus_cases[0]));
  tcase_add_test(tc, opus_head_is_built_once);
  tcase_add_loop_test(tc, opus_ts_control_header_is_stripped, 0, 9);
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
  tcase_add_test(tc, ac3_bit_rate_code_is_half_frmsizecod);
  tcase_add_test(tc, adts_counts_raw_data_blocks_per_frame);
  tcase_add_test(tc, eac3_dependent_substream_joins_the_access_unit);
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
