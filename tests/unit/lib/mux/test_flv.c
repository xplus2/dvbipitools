/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/mux/flv/flv.h"
#include "lib/mux/psi_build.h"

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, int pusi, const unsigned char *payload, size_t plen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memcpy(pkt + 4, payload, plen);
  for (i = 4 + plen; i < 188; i++)
    pkt[i] = 0xFF;
}

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  unsigned char payload[184];
  payload[0] = 0x00;
  memcpy(payload + 1, section, slen);
  wrap_ts_packet(pkt, pid, 1, payload, slen + 1);
}

/* one-ES PMT for prog_num, single ES at es_pid, given stream_type */
static size_t build_pmt_one_es(unsigned char *out, unsigned prog_num, unsigned pmt_pid, unsigned es_pid, unsigned char stream_type) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  (void)pmt_pid;
  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = stream_type;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

/* 0x0F=AAC ADTS, 0x81=AC-3, no registration descriptor: psi.c's classifier
   takes raw stream_type, same simplification as test_mkv.c */
static void feed_discovery(unsigned pid, unsigned char stream_type, void (*feed)(void *, const unsigned char *), void *ctx) {
  unsigned char sec[256], pkt[188];
  size_t slen;

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  feed(ctx, pkt);

  slen = build_pmt_one_es(sec, 101, 0x0100, pid, stream_type);
  wrap_section_packet(pkt, 0x0100, sec, slen);
  feed(ctx, pkt);
}

static void flv_feed_adapter(void *ctx, const unsigned char *pkt) { flv_feed((flv_t *)ctx, pkt); }

static size_t build_pes_with_pts(unsigned char *out, uint64_t pts_90k, const unsigned char *payload, size_t plen) {
  size_t n = 0;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x01;
  out[n++] = 0xC0;
  out[n++] = (unsigned char)((8 + plen) >> 8);
  out[n++] = (unsigned char)(8 + plen);
  out[n++] = 0x80;
  out[n++] = 0x80;
  out[n++] = 0x05;
  out[n++] = (unsigned char)(0x21 | ((pts_90k >> 29) & 0x0E));
  out[n++] = (unsigned char)(pts_90k >> 22);
  out[n++] = (unsigned char)(((pts_90k >> 14) & 0xFE) | 0x01);
  out[n++] = (unsigned char)(pts_90k >> 7);
  out[n++] = (unsigned char)(((pts_90k << 1) & 0xFE) | 0x01);
  memcpy(out + n, payload, plen);
  n += plen;
  return n;
}

/* one-frame ADTS AAC, 44100 Hz (sr_idx=4), raw_blocks=0 */
static size_t build_adts_frame(unsigned char *out, size_t total_len) {
  size_t i;
  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = (unsigned char)(0x40 | (4 << 2));
  out[3] = (unsigned char)((total_len >> 11) & 0x03);
  out[4] = (unsigned char)((total_len >> 3) & 0xFF);
  out[5] = (unsigned char)((total_len & 0x07) << 5);
  out[6] = 0x00;
  for (i = 7; i < total_len; i++)
    out[i] = 0xAB;
  return total_len;
}

/* one AC-3 sync frame, 48 kHz (fscod=0), frmsizecod=0 -> 128 bytes, 2ch (acmod=2) */
static size_t build_ac3_frame(unsigned char *out, size_t total_len) {
  size_t i;
  out[0] = 0x0B;
  out[1] = 0x77;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = 0x00; /* fscod=0, frmsizecod=0 */
  out[5] = 0x00;
  out[6] = 0x40; /* acmod=2 -> ac3_ch[2] = 2 channels */
  for (i = 7; i < total_len; i++)
    out[i] = 0xCD;
  return total_len;
}

#define CAP_MAX 8
typedef struct {
  flv_tag_type_t type[CAP_MAX];
  uint32_t ts[CAP_MAX];
  unsigned char data[CAP_MAX][2048];
  size_t len[CAP_MAX];
  int n;
} capture_t;

static void capture_cb(void *ctx, flv_tag_type_t type, uint32_t ts, const unsigned char *data, size_t len) {
  capture_t *c = ctx;
  if (c->n >= CAP_MAX)
    return;
  c->type[c->n] = type;
  c->ts[c->n] = ts;
  c->len[c->n] = len > sizeof c->data[0] ? sizeof c->data[0] : len;
  memcpy(c->data[c->n], data, c->len[c->n]);
  c->n++;
}

static flv_opts_t base_opts(void) {
  flv_opts_t o;
  memset(&o, 0, sizeof o);
  return o;
}

START_TEST(flv_aac_emits_metadata_seqhdr_and_raw_frame_tags) {
  unsigned long long bytes = 0;
  flv_opts_t opts = base_opts();
  capture_t cap;
  flv_t *f;
  unsigned char adts[64], pes[128], pkt[188];
  size_t alen, plen;

  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  feed_discovery(0x0101, 0x0F /* AAC ADTS */, flv_feed_adapter, f);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);
  flv_close(f); /* flush: single-frame test, no follow-up PES */

  ck_assert_int_eq(flv_error(f), 0);
  ck_assert_int_eq(cap.n, 3);
  ck_assert_int_eq(cap.type[0], FLV_TAG_SCRIPT);
  ck_assert_ptr_nonnull(memmem(cap.data[0], cap.len[0], "onMetaData", 10));
  /* videocodecid=0.0: real ingest servers key audio-only detection off this */
  ck_assert_ptr_nonnull(memmem(cap.data[0], cap.len[0], "videocodecid", 12));

  ck_assert_int_eq(cap.type[1], FLV_TAG_AUDIO);
  ck_assert_uint_eq(cap.len[1], 2 + 2); /* header(2) + 2-byte AAC AudioSpecificConfig */
  ck_assert_uint_eq(cap.data[1][0], 0xAF); /* SoundFormat=10(AAC)<<4 | 0x0F */
  ck_assert_uint_eq(cap.data[1][1], 0x00); /* AACPacketType = sequence header */

  ck_assert_int_eq(cap.type[2], FLV_TAG_AUDIO);
  ck_assert_uint_eq(cap.data[2][0], 0xAF);
  ck_assert_uint_eq(cap.data[2][1], 0x01); /* AACPacketType = raw */
  ck_assert_uint_eq(cap.len[2], 2 + (alen - 7)); /* header(2) + ADTS-stripped payload */
}
END_TEST

START_TEST(flv_ac3_uses_enhanced_rtmp_tag_with_fourcc) {
  unsigned long long bytes = 0;
  flv_opts_t opts = base_opts();
  capture_t cap;
  flv_t *f;
  unsigned char ac3[128], pes[160], pkt[188];
  size_t clen, plen;

  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  feed_discovery(0x0101, 0x81 /* AC-3 */, flv_feed_adapter, f);
  clen = build_ac3_frame(ac3, 128);
  plen = build_pes_with_pts(pes, 90000, ac3, clen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);
  flv_close(f); /* flush: single-frame test, no follow-up PES */

  ck_assert_int_eq(flv_error(f), 0);
  ck_assert_int_eq(cap.n, 3);

  ck_assert_int_eq(cap.type[1], FLV_TAG_AUDIO);
  ck_assert_uint_eq(cap.len[1], 5); /* ExAudioTagHeader byte + 4-byte FourCC, empty seq-start payload */
  ck_assert_uint_eq(cap.data[1][0], 0x90); /* SoundFormat=9(ExHeader)<<4 | AudioPacketType=0(SequenceStart) */
  ck_assert_mem_eq(cap.data[1] + 1, "ac-3", 4);

  ck_assert_int_eq(cap.type[2], FLV_TAG_AUDIO);
  ck_assert_uint_eq(cap.data[2][0], 0x91); /* AudioPacketType=1(CodedFrames) */
  ck_assert_mem_eq(cap.data[2] + 1, "ac-3", 4);
  ck_assert_uint_eq(cap.len[2], 5 + clen); /* header(5) + raw AC-3 sync frame, unmodified */
}
END_TEST

START_TEST(flv_unsupported_audio_codec_is_skipped_without_error) {
  unsigned long long bytes = 0;
  flv_opts_t opts = base_opts();
  capture_t cap;
  flv_t *f;
  unsigned char sec[256], pkt[188];
  size_t slen;

  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  /* 0x04=MPEG-2 Layer 2: no FLV slot, no Enhanced FourCC, flv=no_ac skip not error */
  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  flv_feed(f, pkt);
  slen = build_pmt_one_es(sec, 101, 0x0100, 0x0101, 0x04);
  wrap_section_packet(pkt, 0x0100, sec, slen);
  flv_feed(f, pkt);

  ck_assert_int_eq(flv_error(f), 0);
  ck_assert_int_eq(cap.n, 0); /* no track selected: nothing ever emitted, not even onMetaData */

  flv_close(f);
}
END_TEST

static Suite *flv_suite(void) {
  Suite *s = suite_create("flv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, flv_aac_emits_metadata_seqhdr_and_raw_frame_tags);
  tcase_add_test(tc, flv_ac3_uses_enhanced_rtmp_tag_with_fourcc);
  tcase_add_test(tc, flv_unsupported_audio_codec_is_skipped_without_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(flv_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
