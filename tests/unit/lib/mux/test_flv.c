/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/mux/flv/flv.h"
#include "lib/mux/psi_build.h"

/* Only audio path (video needs H.264/HEVC SPS bitstreams). flv.c discovers PAT/PMT/SDT itself via flv_feed */
static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, int pusi, const unsigned char *payload, size_t plen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memcpy(pkt + 4, payload, plen);
  for (size_t i = 4 + plen; i < 188; i++)
    pkt[i] = 0xFF;
}

static void wrap_section_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  unsigned char payload[184];
  payload[0] = 0x00; /* pointer_field */
  memcpy(payload + 1, section, slen);
  wrap_ts_packet(pkt, pid, 1, payload, slen + 1);
}

/* one-frame ADTS AAC, 44100 Hz (sr_idx=4), raw_blocks=0 */
static size_t build_adts_frame(unsigned char *out, size_t total_len) {
  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = (unsigned char)(0x40 | (4 << 2));
  out[3] = (unsigned char)((total_len >> 11) & 0x03);
  out[4] = (unsigned char)((total_len >> 3) & 0xFF);
  out[5] = (unsigned char)((total_len & 0x07) << 5);
  out[6] = 0x00;
  for (size_t i = 7; i < total_len; i++)
    out[i] = 0xAB;
  return total_len;
}

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

static void feed_discovery(flv_t *f) {
  unsigned char sec[256], pkt[188];
  size_t slen;

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  flv_feed(f, pkt);

  {
    unsigned char body[32];
    size_t n = 0, hdr, crc_at;
    uint32_t crc;
    body[n++] = (unsigned char)(101 >> 8);
    body[n++] = (unsigned char)101;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    body[n++] = 0x0F; /* AAC */
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    hdr = n + 4;
    sec[0] = 0x02;
    sec[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
    sec[2] = (unsigned char)hdr;
    memcpy(sec + 3, body, n);
    crc_at = 3 + n;
    crc = crc32_mpeg(sec, crc_at);
    sec[crc_at + 0] = (unsigned char)(crc >> 24);
    sec[crc_at + 1] = (unsigned char)(crc >> 16);
    sec[crc_at + 2] = (unsigned char)(crc >> 8);
    sec[crc_at + 3] = (unsigned char)crc;
    slen = crc_at + 4;
  }
  wrap_section_packet(pkt, 0x0100, sec, slen);
  flv_feed(f, pkt);

  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  flv_feed(f, pkt);
}

typedef struct {
  flv_tag_type_t type;
  uint32_t timestamp_ms;
  unsigned char data[256];
  size_t len;
} captured_tag_t;

typedef struct {
  captured_tag_t tags[32];
  int n;
} tag_capture_t;

static void capture_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len) {
  tag_capture_t *c = ctx;
  if (c->n >= (int)(sizeof c->tags / sizeof c->tags[0]))
    return;
  c->tags[c->n].type = type;
  c->tags[c->n].timestamp_ms = timestamp_ms;
  c->tags[c->n].len = len < sizeof c->tags[0].data ? len : sizeof c->tags[0].data;
  memcpy(c->tags[c->n].data, data, c->tags[c->n].len);
  c->n++;
}

static int has_tag_type(const tag_capture_t *c, flv_tag_type_t type) {
  for (int i = 0; i < c->n; i++)
    if (c->tags[i].type == type)
      return 1;
  return 0;
}

START_TEST(flv_emits_metadata_and_audio_tags_for_audio_only) {
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char adts[64], pes[128], pkt[188];
  size_t alen, plen;

  memset(&opts, 0, sizeof opts);
  memset(&cap, 0, sizeof cap);

  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  feed_discovery(f);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);
  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);

  ck_assert(cap.n > 0);
  ck_assert(has_tag_type(&cap, FLV_TAG_SCRIPT));
  ck_assert(has_tag_type(&cap, FLV_TAG_AUDIO));

  for (int i = 0; i < cap.n; i++) {
    if (cap.tags[i].type == FLV_TAG_SCRIPT) {
      ck_assert_ptr_nonnull(memmem(cap.tags[i].data, cap.tags[i].len, "onMetaData", 10));
      break;
    }
  }
}
END_TEST

START_TEST(flv_no_supported_tracks_emits_nothing_and_no_error) {
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char sec[256], pkt[188];
  size_t slen;

  memset(&opts, 0, sizeof opts);
  opts.audio_track = 99; /* no such track: the one AAC ES won't be selected */
  memset(&cap, 0, sizeof cap);

  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  flv_feed(f, pkt);

  {
    unsigned char body[32];
    size_t n = 0, hdr, crc_at;
    uint32_t crc;
    body[n++] = (unsigned char)(101 >> 8);
    body[n++] = (unsigned char)101;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    body[n++] = 0x0F;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    hdr = n + 4;
    sec[0] = 0x02;
    sec[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
    sec[2] = (unsigned char)hdr;
    memcpy(sec + 3, body, n);
    crc_at = 3 + n;
    crc = crc32_mpeg(sec, crc_at);
    sec[crc_at + 0] = (unsigned char)(crc >> 24);
    sec[crc_at + 1] = (unsigned char)(crc >> 16);
    sec[crc_at + 2] = (unsigned char)(crc >> 8);
    sec[crc_at + 3] = (unsigned char)crc;
    slen = crc_at + 4;
  }
  wrap_section_packet(pkt, 0x0100, sec, slen);
  flv_feed(f, pkt);
  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);

  ck_assert_int_eq(cap.n, 0);
}
END_TEST

static Suite *flv_suite(void) {
  Suite *s = suite_create("flv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, flv_emits_metadata_and_audio_tags_for_audio_only);
  tcase_add_test(tc, flv_no_supported_tracks_emits_nothing_and_no_error);
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
