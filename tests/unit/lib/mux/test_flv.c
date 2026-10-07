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

#include "ts_test_util.h"

static void feed_discovery(flv_t *f) {
  unsigned char pkts[DISCOVERY_PACKETS][188];

  build_aac_discovery(pkts);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) flv_feed(f, pkts[i]);
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

static void capture_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *hdr, size_t hn, const unsigned char *payload, size_t pn) {
  tag_capture_t *c = ctx;
  size_t len = hn + pn;
  size_t take_h;
  size_t take_p;
  if (c->n >= (int)(sizeof c->tags / sizeof c->tags[0]))
    return;
  c->tags[c->n].type = type;
  c->tags[c->n].timestamp_ms = timestamp_ms;
  c->tags[c->n].len = len < sizeof c->tags[0].data ? len : sizeof c->tags[0].data;
  take_h = hn < c->tags[c->n].len ? hn : c->tags[c->n].len;
  take_p = c->tags[c->n].len - take_h;
  if (take_h) memcpy(c->tags[c->n].data, hdr, take_h);
  if (take_p) memcpy(c->tags[c->n].data + take_h, payload, take_p);
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
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  size_t alen;
  size_t plen;

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
  unsigned char sec[256];
  unsigned char pkt[188];
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
    size_t n = 0;
    size_t hdr;
    size_t crc_at;
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

START_TEST(flv_emits_vvc1_fourcc_and_vvcc_seqhdr) {
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char sec[256];
  unsigned char pkt[188];
  unsigned char au[64];
  unsigned char pes[128];
  size_t slen;
  size_t alen;
  size_t plen;
  static const unsigned char expect_vvcc[] = {
    0xFE, 0x03,
    0x8E, 0x00, 0x01, 0x00, 0x04, 0x00, 0x71, 0xAA, 0xBB,
    0x8F, 0x00, 0x01, 0x00, 0x09, 0x00, 0x79, 0x11, 0x0B, 0xFF, 0xFF, 0xDF, 0x00, 0x12,
    0x90, 0x00, 0x01, 0x00, 0x04, 0x00, 0x81, 0xCC, 0xDD,
  };
  int found = 0;
  memset(&opts, 0, sizeof opts);
  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);
  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  flv_feed(f, pkt);
  slen = build_pmt_video(sec, 101, 0x0100, 0x33 /* VVC */);
  wrap_section_packet(pkt, 0x0100, sec, slen);
  flv_feed(f, pkt);

  /* AU1: VPS+SPS+PPS+IDR bundled, buffered */
  alen = build_vvc_au(au);
  plen = build_pes_with_pts(pes, 90000, au, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);
  alen = build_vvc_au(au);
  plen = build_pes_with_pts(pes, 93000, au, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);

  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);
  for (int i = 0; i < cap.n; i++) {
    const captured_tag_t *tag = &cap.tags[i];
    if (tag->type != FLV_TAG_VIDEO || tag->len < 5) continue;
    if (tag->data[0] != 0x90) continue; /* ExVideoHeader | FrameType=key | PacketType=SequenceStart */
    if (memcmp(tag->data + 1, "vvc1", 4) != 0) continue;
    ck_assert_uint_ge(tag->len, 5 + sizeof expect_vvcc);
    ck_assert_mem_eq(tag->data + 5, expect_vvcc, sizeof expect_vvcc);
    found = 1;
    break;
  }
  ck_assert(found);
}
END_TEST

START_TEST(flv_emits_av01_fourcc_and_av1c_seqhdr) {
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char sec[256];
  unsigned char pkt[188];
  unsigned char au[64];
  unsigned char pes[128];
  size_t slen;
  size_t alen;
  size_t plen;
  static const unsigned char expect_av1c[] = {
    0x81, 0x00, 0x0C, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01,
  };
  int found = 0;

  memset(&opts, 0, sizeof opts);
  memset(&cap, 0, sizeof cap);

  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  flv_feed(f, pkt);
  slen = build_pmt_av1_video(sec, 101, 0x0100);
  wrap_section_packet(pkt, 0x0100, sec, slen);
  flv_feed(f, pkt);

  alen = build_av1_au(au);
  plen = build_pes_with_pts(pes, 90000, au, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);

  alen = build_av1_au(au);
  plen = build_pes_with_pts(pes, 93000, au, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  flv_feed(f, pkt);

  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);

  for (int i = 0; i < cap.n; i++) {
    const captured_tag_t *tag = &cap.tags[i];
    if (tag->type != FLV_TAG_VIDEO || tag->len < 5) continue;
    if (tag->data[0] != 0x90) continue;
    if (memcmp(tag->data + 1, "av01", 4) != 0) continue;
    ck_assert_uint_ge(tag->len, 5 + sizeof expect_av1c);
    ck_assert_mem_eq(tag->data + 5, expect_av1c, sizeof expect_av1c);
    found = 1;
    break;
  }
  ck_assert(found);
}
END_TEST

START_TEST(flv_edge_case_streams_never_error_and_drop_unusable_frames) {
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char pkts[EDGE_MAX_PACKETS][188];
  size_t count = build_edge_packets((edge_case_t)_i, pkts);
  int frame_tags = 0;

  memset(&opts, 0, sizeof opts);
  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);
  feed_discovery(f);
  for (size_t i = 0; i < count; i++) flv_feed(f, pkts[i]);
  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);

  for (int i = 0; i < cap.n; i++)
    if (cap.tags[i].type == FLV_TAG_AUDIO && buffer_has_frame_marker(cap.tags[i].data, cap.tags[i].len)) frame_tags++;
  ck_assert_int_eq(frame_tags, edge_expects_frame[_i]);
}
END_TEST

#define VIDEO_AUS 4

typedef struct {
  unsigned char stream_type;
  size_t (*build_au)(unsigned char *out, int idr);
  unsigned char seq_first_byte;
  unsigned char key_first_byte;
  unsigned char inter_first_byte;
  const char *fourcc;
  const unsigned char *sps;
  size_t sps_len;
} flv_video_case_t;

static const flv_video_case_t flv_video_cases[] = {
    {0x1B, build_h264_au, 0x17, 0x17, 0x27, NULL, h264_sps_1080p, sizeof h264_sps_1080p},
    {0x24, build_hevc_au, 0x90, 0x93, 0xA3, "hvc1", hevc_sps_1080p, sizeof hevc_sps_1080p},
};

START_TEST(flv_video_sequence_header_and_coded_frames) {
  const flv_video_case_t *c = &flv_video_cases[_i];
  unsigned long long bytes = 0;
  flv_opts_t opts;
  flv_t *f;
  tag_capture_t cap;
  unsigned char pkts[DISCOVERY_PACKETS][188];
  unsigned char au[128];
  unsigned char pes[188];
  unsigned char pkt[188];
  size_t cfg_off = 5;
  int seq_seen = 0;
  unsigned key_frames = 0;
  unsigned inter_frames = 0;

  memset(&opts, 0, sizeof opts);
  memset(&cap, 0, sizeof cap);
  f = flv_new(&opts, 0, capture_cb, &cap, &bytes);
  ck_assert_ptr_nonnull(f);
  build_video_discovery(pkts, 0, c->stream_type);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) flv_feed(f, pkts[i]);
  for (unsigned i = 0; i < VIDEO_AUS; i++) {
    size_t alen = c->build_au(au, i == 0);
    size_t plen = build_pes_with_pts_dts(pes, 90000 + i * 3000, 90000 + i * 3000, au, alen);

    wrap_ts_packet_exact(pkt, 0x0101, 1, pes, plen);
    flv_feed(f, pkt);
  }
  ck_assert_int_eq(flv_error(f), 0);
  flv_close(f);

  for (int i = 0; i < cap.n; i++) {
    const captured_tag_t *tag = &cap.tags[i];

    if (tag->type != FLV_TAG_VIDEO || tag->len < cfg_off) continue;
    if (c->fourcc) ck_assert_mem_eq(tag->data + 1, c->fourcc, 4);
    if (tag->data[0] == c->seq_first_byte && !seq_seen) {
      ck_assert_int_eq(tag->data[cfg_off], 0x01);
      ck_assert_ptr_nonnull(memmem(tag->data + cfg_off, tag->len - cfg_off, c->sps, c->sps_len));
      seq_seen = 1;
    } else if (tag->data[0] == c->key_first_byte) {
      key_frames++;
    } else if (tag->data[0] == c->inter_first_byte) {
      inter_frames++;
    }
  }
  ck_assert_int_eq(seq_seen, 1);
  ck_assert_uint_eq(key_frames, 1u);
  ck_assert_uint_eq(inter_frames, VIDEO_AUS - 2);
}
END_TEST

static Suite *flv_suite(void) {
  Suite *s = suite_create("flv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, flv_emits_metadata_and_audio_tags_for_audio_only);
  tcase_add_test(tc, flv_no_supported_tracks_emits_nothing_and_no_error);
  tcase_add_test(tc, flv_emits_vvc1_fourcc_and_vvcc_seqhdr);
  tcase_add_test(tc, flv_emits_av01_fourcc_and_av1c_seqhdr);
  tcase_add_loop_test(tc, flv_edge_case_streams_never_error_and_drop_unusable_frames, 0, EDGE_COUNT);
  tcase_add_loop_test(tc, flv_video_sequence_header_and_coded_frames, 0, (int)(sizeof flv_video_cases / sizeof flv_video_cases[0]));
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
