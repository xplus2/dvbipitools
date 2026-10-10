/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/demux/crc32.h"
#include "lib/mux/mp4/mp4.h"
#include "lib/mux/psi_build.h"

#include "ts_test_util.h"

static mp4_opts_t base_cfg(void) {
  mp4_opts_t opts;
  memset(&opts, 0, sizeof opts);
  opts.audio_all = 1;
  return opts;
}

/* feeds PAT (program 101 -> PMT pid 0x100), a 1-audio-ES PMT (AAC, pid 0x101),
   and an SDT for program 101 (mp4 doesn't wait ~2s real time for one) */
static void feed_discovery(mp4_t *m) {
  unsigned char pkts[DISCOVERY_PACKETS][188];

  build_aac_discovery(pkts);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) mp4_feed(m, pkts[i]);
}

START_TEST(mp4_writes_a_valid_container_for_audio_only) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  size_t alen;
  size_t plen;
  FILE *f;
  unsigned char *buf;
  long fsize;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0 /* audio-only, m4a */, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);

  feed_discovery(m);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mp4_feed(m, pkt);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  ck_assert(fsize > 0);
  buf = malloc((size_t)fsize);
  ck_assert_ptr_nonnull(buf);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);

  ck_assert(fsize >= 8);
  /* first box: size(4) + "ftyp" fourcc */
  ck_assert_int_eq(memcmp(buf + 4, "ftyp", 4), 0);
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "moov", 4));
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "mdat", 4));
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "mp4a", 4));
  free(buf);
  unlink(path);
}
END_TEST

START_TEST(mp4_no_supported_tracks_writes_nothing_and_no_error) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char sec[256];
  unsigned char pkt[188];
  size_t slen;
  long fsize;
  FILE *f;

  cfg.audio_all = 0;
  cfg.audio_track = 99; /* no such track: the one AAC ES won't be selected */

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);

  /* PAT+PMT only: no track selected, so mp4 never reaches the SDT wait */
  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  mp4_feed(m, pkt);

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
  mp4_feed(m, pkt);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);
  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fclose(f);
  ck_assert_int_eq(fsize, 0);

  unlink(path);
}
END_TEST

/* one-AAC-ES PMT for prog_num, ES pid = pmt_pid+1 */
static size_t build_pmt_aac(unsigned char *out, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  unsigned es_pid = pmt_pid + 1;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((es_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)es_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x0F; /* AAC */
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

START_TEST(mp4_multi_program_writes_two_audio_tracks) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char sec[256];
  unsigned char pkt[188];
  unsigned char adts[64];
  unsigned char pes[128];
  size_t slen;
  size_t alen;
  size_t plen;
  psi_pat_entry_t progs[2];
  unsigned pmt_pids[2] = {0x0100, 0x0200};
  FILE *f;
  unsigned char *buf;
  long fsize;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, pmt_pids, 2);
  ck_assert_ptr_nonnull(m);

  progs[0].program_number = 101;
  progs[0].pmt_pid = 0x0100;
  progs[1].program_number = 102;
  progs[1].pmt_pid = 0x0200;
  slen = psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  mp4_feed(m, pkt);

  slen = build_pmt_aac(sec, 101, 0x0100);
  wrap_section_packet(pkt, 0x0100, sec, slen);
  mp4_feed(m, pkt);
  slen = build_pmt_aac(sec, 102, 0x0200);
  wrap_section_packet(pkt, 0x0200, sec, slen);
  mp4_feed(m, pkt);

  slen = psi_build_sdt(0, 0x1234, 5, 101, 0x01, "P", "One", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  mp4_feed(m, pkt);
  slen = psi_build_sdt(0, 0x1234, 5, 102, 0x01, "P", "Two", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  mp4_feed(m, pkt);

  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mp4_feed(m, pkt);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0201, 1, pes, plen);
  mp4_feed(m, pkt);

  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  ck_assert(fsize > 0);
  buf = malloc((size_t)fsize);
  ck_assert_ptr_nonnull(buf);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);

  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "moov", 4));
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "mdat", 4));
  free(buf);
  unlink(path);
}
END_TEST

START_TEST(mp4_edge_case_streams_never_error_and_drop_unusable_frames) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char pkts[EDGE_MAX_PACKETS][188];
  size_t count = build_edge_packets((edge_case_t)_i, pkts);
  unsigned char *buf;
  size_t len = 0;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_discovery(m);
  for (size_t i = 0; i < count; i++) mp4_feed(m, pkts[i]);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);

  buf = slurp_file(path, &len);
  ck_assert_ptr_nonnull(buf);
  ck_assert_int_eq(buffer_has_frame_marker(buf, len), edge_expects_frame[_i]);
  if (edge_expects_frame[_i]) {
    ck_assert_uint_ge(len, 8u);
    ck_assert_int_eq(memcmp(buf + 4, "ftyp", 4), 0);
    ck_assert_ptr_nonnull(memmem(buf, len, "mp4a", 4));
  }
  free(buf);
  unlink(path);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char stream_type;
  int av1;
  size_t (*build_au)(unsigned char *out, int idr);
  const char *sample_entry;
  const char *config_box;
  unsigned markers;
} video_case_t;

static size_t vvc_au_any(unsigned char *out, int idr) {
  (void)idr;
  return build_vvc_au(out);
}

static size_t av1_au_any(unsigned char *out, int idr) {
  (void)idr;
  return build_av1_au(out);
}

#define VIDEO_AUS 4

static const video_case_t video_cases[] = {
  {"h264", 0x1B, 0, build_h264_au, "avc1", "avcC", VIDEO_AUS - 1},
  {"hevc", 0x24, 0, build_hevc_au, "hvc1", "hvcC", VIDEO_AUS - 1},
  {"vvc", 0x33, 0, vvc_au_any, "vvc1", "vvcC", 0},
  {"av1", 0, 1, av1_au_any, "av01", "av1C", 0},
};

START_TEST(mp4_video_header_parse_writes_sample_entry_and_samples) {
  const video_case_t *c = &video_cases[_i];
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char pkts[DISCOVERY_PACKETS][188];
  unsigned char au[128];
  unsigned char pes[188];
  unsigned char pkt[188];
  unsigned char *buf;
  size_t len = 0;
  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 1, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  build_video_discovery(pkts, c->av1, c->stream_type);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) mp4_feed(m, pkts[i]);
  for (unsigned i = 0; i < VIDEO_AUS; i++) {
    size_t alen = c->build_au(au, i == 0);
    size_t plen = build_pes_with_pts_dts(pes, 90000 + i * 3000, 90000 + i * 3000, au, alen);

    wrap_ts_packet_exact(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);
  buf = slurp_file(path, &len);
  ck_assert_ptr_nonnull(buf);
  ck_assert_int_eq(memcmp(buf + 4, "ftyp", 4), 0);
  ck_assert_ptr_nonnull(memmem(buf, len, "moov", 4));
  ck_assert_ptr_nonnull(memmem(buf, len, c->sample_entry, 4));
  ck_assert_ptr_nonnull(memmem(buf, len, c->config_box, 4));
  ck_assert_ptr_nonnull(memmem(buf, len, "stts", 4));
  ck_assert_uint_eq(count_frame_markers(buf, len), c->markers);
  free(buf);
  unlink(path);
}
END_TEST

static unsigned char *write_h264_stream(const unsigned pts_lead_90k[VIDEO_AUS], size_t *len_out) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char pkts[DISCOVERY_PACKETS][188];
  unsigned char au[128];
  unsigned char pes[188];
  unsigned char pkt[188];
  unsigned char *buf;
  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 1, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  build_video_discovery(pkts, 0, 0x1B);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) mp4_feed(m, pkts[i]);
  for (unsigned i = 0; i < VIDEO_AUS; i++) {
    size_t alen = build_h264_au(au, i == 0);
    size_t plen = build_pes_with_pts_dts(pes, 90000 + i * 3000 + pts_lead_90k[i], 90000 + i * 3000, au, alen);
    wrap_ts_packet_exact(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  mp4_close(m);
  close(fd);
  buf = slurp_file(path, len_out);
  ck_assert_ptr_nonnull(buf);
  unlink(path);
  return buf;
}

START_TEST(mp4_video_with_reordering_writes_ctts_with_runs) {
  static const unsigned lead[VIDEO_AUS] = {0, 6000, 6000, 0};
  size_t len = 0;
  unsigned char *buf = write_h264_stream(lead, &len);
  unsigned char *ctts = memmem(buf, len, "ctts", 4);
  ck_assert_ptr_nonnull(ctts);
  ck_assert_uint_eq(((unsigned)ctts[8] << 24) | ((unsigned)ctts[9] << 16) | ((unsigned)ctts[10] << 8) | ctts[11], 2u);
  free(buf);
}
END_TEST

START_TEST(mp4_video_without_reordering_omits_ctts) {
  static const unsigned lead[VIDEO_AUS] = {0, 0, 0, 0};
  size_t len = 0;
  unsigned char *buf = write_h264_stream(lead, &len);
  ck_assert_ptr_null(memmem(buf, len, "ctts", 4));
  free(buf);
}
END_TEST

START_TEST(mp4_selects_only_the_requested_audio_track) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  unsigned char *buf;
  size_t len = 0;
  size_t alen;
  size_t plen;

  cfg.audio_all = 0;
  cfg.audio_track = 1;
  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_discovery(m);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mp4_feed(m, pkt);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);
  buf = slurp_file(path, &len);
  ck_assert_ptr_nonnull(buf);
  ck_assert_int_eq(memcmp(buf + 4, "ftyp", 4), 0);
  ck_assert_ptr_nonnull(memmem(buf, len, "mp4a", 4));
  free(buf);
  unlink(path);
}
END_TEST

START_TEST(mp4_unsupported_video_codec_writes_nothing_and_no_error) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char pkts[DISCOVERY_PACKETS][188];
  unsigned char *buf;
  size_t len = 1;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 1, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  build_video_discovery(pkts, 0, 0x02);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) mp4_feed(m, pkts[i]);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);
  buf = slurp_file(path, &len);
  ck_assert_uint_eq(len, 0u);
  free(buf);
  unlink(path);
}
END_TEST

static Suite *mp4_suite(void) {
  Suite *s = suite_create("mp4");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mp4_writes_a_valid_container_for_audio_only);
  tcase_add_test(tc, mp4_no_supported_tracks_writes_nothing_and_no_error);
  tcase_add_test(tc, mp4_multi_program_writes_two_audio_tracks);
  tcase_add_loop_test(tc, mp4_edge_case_streams_never_error_and_drop_unusable_frames, 0, EDGE_COUNT);
  tcase_add_loop_test(tc, mp4_video_header_parse_writes_sample_entry_and_samples, 0, (int)(sizeof video_cases / sizeof video_cases[0]));
  tcase_add_test(tc, mp4_video_with_reordering_writes_ctts_with_runs);
  tcase_add_test(tc, mp4_video_without_reordering_omits_ctts);
  tcase_add_test(tc, mp4_selects_only_the_requested_audio_track);
  tcase_add_test(tc, mp4_unsupported_video_codec_writes_nothing_and_no_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mp4_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
