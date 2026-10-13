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

START_TEST(mp4_audio_duration_has_no_per_frame_truncation) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char es[40 * 30];
  unsigned char pes[sizeof es + 16];
  unsigned char pkt[188];
  size_t plen;
  FILE *f;
  unsigned char *buf;
  unsigned char *p;
  long fsize;
  uint32_t dur;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_discovery(m);
  for (size_t i = 0; i < 40; i++) build_adts_frame(es + i * 30, 30);
  plen = build_pes_with_pts(pes, 90000, es, sizeof es);
  for (size_t off = 0; off < plen; off += 184) {
    size_t n = plen - off < 184 ? plen - off : 184;
    wrap_ts_packet(pkt, 0x0101, off == 0, pes + off, n);
    mp4_feed(m, pkt);
  }
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  buf = malloc((size_t)fsize);
  ck_assert_ptr_nonnull(buf);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);
  p = memmem(buf, (size_t)fsize, "mdhd", 4);
  ck_assert_ptr_nonnull(p);
  dur = (uint32_t)p[20] << 24 | (uint32_t)p[21] << 16 | (uint32_t)p[22] << 8 | p[23];
  /* 40 * 1024 / 44100 s = 928.8 ms. per-frame truncation gave 920 */
  ck_assert_uint_ge(dur, 928);
  ck_assert_uint_le(dur, 929);
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

START_TEST(mp4_pending_overflow_leaves_out_tracks_without_header) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char sec[256];
  unsigned char pkt[188];
  unsigned char adts[256];
  unsigned char pes[256];
  size_t slen;
  psi_pat_entry_t progs[2];
  unsigned pmt_pids[2] = {0x0100, 0x0200};
  FILE *f;
  unsigned char *buf;
  long fsize;
  size_t pos = 0;
  int traks = 0;

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
  for (unsigned k = 0; k < 1100; k++) { /* pid 0x0201 stays silent */
    size_t alen = build_adts_frame(adts, 170);
    size_t plen = build_pes_with_pts(pes, 90000u + k * 1920u, adts, alen);
    wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  buf = malloc((size_t)fsize);
  ck_assert_ptr_nonnull(buf);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);
  while (pos + 4 <= (size_t)fsize) {
    const unsigned char *p = memmem(buf + pos, (size_t)fsize - pos, "trak", 4);
    if (!p) break;
    traks++;
    pos = (size_t)(p - buf) + 4;
  }
  ck_assert_int_eq(traks, 1);
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

START_TEST(mp4_video_reorder_delay_is_skipped_by_edit_list) {
  static const unsigned lead[VIDEO_AUS] = {3600, 3600, 3600, 3600};
  size_t len = 0;
  unsigned char *buf = write_h264_stream(lead, &len);
  unsigned char *elst = memmem(buf, len, "elst", 4);
  ck_assert_ptr_nonnull(elst);
  ck_assert_uint_eq(((unsigned)elst[8] << 24) | ((unsigned)elst[9] << 16) | ((unsigned)elst[10] << 8) | elst[11], 1u);
  ck_assert_uint_eq(((unsigned)elst[16] << 24) | ((unsigned)elst[17] << 16) | ((unsigned)elst[18] << 8) | elst[19], 40u);
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

static long file_slurp(const char *path, unsigned char **out) {
  FILE *f = fopen(path, "rb");
  long n;

  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  rewind(f);
  *out = malloc((size_t)n);
  ck_assert_ptr_nonnull(*out);
  ck_assert_uint_eq(fread(*out, 1, (size_t)n, f), (size_t)n);
  fclose(f);
  return n;
}

START_TEST(mp4_checkpoint_makes_unclosed_file_indexed) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  unsigned char *buf;
  long fsize;
  const unsigned char *mdat;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_discovery(m);
  for (unsigned k = 0; k < 4; k++) {
    size_t alen = build_adts_frame(adts, 50);
    size_t plen = build_pes_with_pts(pes, 90000u + k * 270000u, adts, alen);
    wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }

  fsize = file_slurp(path, &buf); /* not closed: SIGKILL equivalent */
  ck_assert_int_eq(memcmp(buf + 4, "ftyp", 4), 0);
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "moov", 4));
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "mp4a", 4));
  mdat = memmem(buf, (size_t)fsize, "mdat", 4);
  ck_assert_ptr_nonnull(mdat);
  ck_assert_uint_eq(mdat[-4] | mdat[-3] | mdat[-2] | mdat[-1], 0u); /* size 0: to EOF */
  free(buf);

  mp4_close(m);
  close(fd);
  fsize = file_slurp(path, &buf);
  mdat = memmem(buf, (size_t)fsize, "mdat", 4);
  ck_assert_ptr_nonnull(mdat);
  ck_assert_uint_eq(mdat[-1], 1u); /* 64 bit size after close */
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "moov", 4));
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

START_TEST(mp4_timestamp_splice_keeps_duration_of_content) {
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
  unsigned char *mvhd;
  size_t len = 0;
  uint32_t dur;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 1, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  build_video_discovery(pkts, 0, 0x1B);
  for (size_t i = 0; i < DISCOVERY_PACKETS; i++) mp4_feed(m, pkts[i]);
  for (unsigned i = 0; i < 6; i++) {
    size_t alen = build_h264_au(au, i == 0);
    unsigned long long ts = 90000ULL + i * 3600ULL + (i >= 3 ? 5000ULL * 90000ULL : 0);
    size_t plen = build_pes_with_pts_dts(pes, ts, ts, au, alen);
    wrap_ts_packet_exact(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  mp4_close(m);
  close(fd);
  buf = slurp_file(path, &len);
  ck_assert_ptr_nonnull(buf);
  mvhd = memmem(buf, len, "mvhd", 4);
  ck_assert_ptr_nonnull(mvhd);
  dur = ((uint32_t)mvhd[20] << 24) | ((uint32_t)mvhd[21] << 16) | ((uint32_t)mvhd[22] << 8) | mvhd[23];
  ck_assert_uint_ge(dur, 100u);
  ck_assert_uint_lt(dur, 1000u); /* ms, 1000 timescale */
  free(buf);
  unlink(path);
}
END_TEST

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

START_TEST(mp4_new_rejects_an_unseekable_output) {
  int fds[2];
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();

  ck_assert_int_eq(pipe(fds), 0);
  ck_assert_ptr_null(mp4_new(fds[1], &cfg, 0, &bytes, NULL, 0));
  ck_assert_uint_eq(bytes, 0u);
  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(mp4_checkpoint_stays_exact_across_relayouts) {
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  unsigned char *buf;
  unsigned char *p;
  long fsize;
  uint32_t n;

  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_discovery(m);
  for (unsigned k = 0; k < 1500; k++) {
    size_t alen = build_adts_frame(adts, 50);
    size_t plen = build_pes_with_pts(pes, 90000u + k * 18000u, adts, alen);
    wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  ck_assert_int_eq(mp4_error(m), 0);

  fsize = file_slurp(path, &buf);
  p = memmem(buf, (size_t)fsize, "stsz", 4);
  ck_assert_ptr_nonnull(p);
  n = rd32(p + 12);
  ck_assert_uint_ge(n, 1250u); /* checkpoint lags by under 5 s of media time */
  ck_assert_uint_le(n, 1500u);
  p = memmem(buf, (size_t)fsize, "stco", 4);
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(rd32(p + 8), n);
  ck_assert_uint_eq(buf[rd32(p + 12)], 0xABu); /* first chunk offset hits AAC payload */
  ck_assert_uint_eq(buf[rd32(p + 12 + 4 * (n - 1))], 0xABu);
  free(buf);

  mp4_close(m);
  close(fd);
  fsize = file_slurp(path, &buf);
  p = memmem(buf, (size_t)fsize, "stsz", 4);
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(rd32(p + 12), 1500u);
  free(buf);
  unlink(path);
}
END_TEST

static unsigned char rev8_local(unsigned char b) {
  b = (unsigned char)((b >> 4) | (b << 4));
  b = (unsigned char)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
  b = (unsigned char)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
  return b;
}

static unsigned char hamm_raw(unsigned d) {
  unsigned D1 = d & 1, D2 = (d >> 1) & 1, D3 = (d >> 2) & 1, D4 = (d >> 3) & 1;
  unsigned P1 = D1 ^ D2 ^ D4, P2 = D1 ^ D3 ^ D4, P3 = D2 ^ D3 ^ D4;
  unsigned c = P1 | (P2 << 1) | (D1 << 2) | (P3 << 3) | (D2 << 4) | (D3 << 5) | (D4 << 6);
  unsigned ones = 0;
  for (int k = 0; k < 7; k++) ones += (c >> k) & 1;
  c |= (ones & 1) << 7;
  return (unsigned char)c;
}

/* one EN 300 472 data unit: magazine 7, packet 1, row text */
static size_t build_ttx_packet(unsigned char *out, unsigned row, const char *text) {
  size_t n = 0;
  size_t tlen = strlen(text);
  out[n++] = 0x10;
  out[n++] = 0x03;
  out[n++] = 0x2C;
  out[n++] = 0xFF;
  out[n++] = 0xFF;
  out[n++] = hamm_raw(7u | ((row & 1u) << 3));
  out[n++] = hamm_raw(row >> 1);
  size_t i = 0;
  if (tlen > 40) tlen = 40;
  for (; i < tlen; i++) out[n++] = rev8_local((unsigned char)text[i]);
  for (; i < 40; i++) out[n++] = rev8_local(0x20);
  if (!row) { /* header of page 777 */
    out[7] = hamm_raw(7);
    out[8] = hamm_raw(7);
  }
  return n;
}

static size_t build_ttx_row(unsigned char *out, const char *text) { return build_ttx_packet(out, 1, text); }

static void feed_aac_and_teletext_discovery(mp4_t *m) {
  unsigned char sec[256];
  unsigned char pkt[188];
  unsigned char body[40];
  size_t n = 0;
  size_t slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  uint32_t crc;

  wrap_section_packet(pkt, 0x0000, sec, slen);
  mp4_feed(m, pkt);
  body[n++] = 0x00;
  body[n++] = 101;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0xE1;
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x0F;
  body[n++] = 0xE1;
  body[n++] = 0x01;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x06;
  body[n++] = 0xE1;
  body[n++] = 0x02;
  body[n++] = 0xF0;
  body[n++] = 0x07;
  body[n++] = 0x56;
  body[n++] = 0x05;
  memcpy(body + n, "eng", 3);
  n += 3;
  body[n++] = (2 << 3) | 7;
  body[n++] = 0x77;
  sec[0] = 0x02;
  sec[1] = (unsigned char)(0xB0 | (((n + 4) >> 8) & 0x0F));
  sec[2] = (unsigned char)(n + 4);
  memcpy(sec + 3, body, n);
  crc = crc32_mpeg(sec, 3 + n);
  sec[3 + n + 0] = (unsigned char)(crc >> 24);
  sec[3 + n + 1] = (unsigned char)(crc >> 16);
  sec[3 + n + 2] = (unsigned char)(crc >> 8);
  sec[3 + n + 3] = (unsigned char)crc;
  wrap_section_packet(pkt, 0x0100, sec, 3 + n + 4);
  mp4_feed(m, pkt);
  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  mp4_feed(m, pkt);
}

START_TEST(mp4_subtitle_gaps_become_empty_samples) {
  static const char *const rows[] = {"ONE", "TWO", "THREE"};
  static const unsigned row_ms[] = {1000, 8000, 15000};
  char path[] = "/tmp/dvbipitools_test_mp4_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mp4_opts_t cfg = base_cfg();
  mp4_t *m;
  unsigned char adts[64];
  unsigned char pes[128];
  unsigned char pkt[188];
  unsigned char *buf;
  unsigned char *p;
  long fsize;
  static const uint32_t want[] = {5, 2, 5, 2, 7};

  ck_assert_int_ge(fd, 0);
  cfg.subs_srt = 1;
  m = mp4_new(fd, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  feed_aac_and_teletext_discovery(m);
  {
    unsigned char hdr[64];
    size_t hlen = build_ttx_packet(hdr, 0, "");
    size_t plen = build_pes_with_pts(pes, 0, hdr, hlen);
    wrap_ts_packet_exact(pkt, 0x0102, 1, pes, plen);
    mp4_feed(m, pkt);
  }
  for (unsigned k = 0, r = 0; k < 720; k++) {
    uint64_t pts = 45000u + (uint64_t)k * 2088u; /* 44.1 kHz AAC frame */
    size_t alen = build_adts_frame(adts, 50);
    size_t plen = build_pes_with_pts(pes, pts, adts, alen);
    wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
    mp4_feed(m, pkt);
    if (r < 3 && pts >= row_ms[r] * 90u) {
      unsigned char row[64];
      size_t rlen = build_ttx_row(row, rows[r]);
      plen = build_pes_with_pts(pes, row_ms[r] * 90u, row, rlen);
      wrap_ts_packet_exact(pkt, 0x0102, 1, pes, plen);
      mp4_feed(m, pkt);
      r++;
    }
  }
  mp4_close(m);
  close(fd);

  fsize = file_slurp(path, &buf);
  p = memmem(buf, (size_t)fsize, "stsz", 4); /* audio */
  ck_assert_ptr_nonnull(p);
  p = memmem(p + 4, (size_t)(buf + fsize - p - 4), "stsz", 4); /* subtitles */
  ck_assert_ptr_nonnull(p);
  ck_assert_uint_eq(rd32(p + 12), 5u);
  for (unsigned i = 0; i < 5; i++) ck_assert_uint_eq(rd32(p + 16 + 4 * i), want[i]);
  free(buf);
  unlink(path);
}
END_TEST

static Suite *mp4_suite(void) {
  Suite *s = suite_create("mp4");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mp4_writes_a_valid_container_for_audio_only);
  tcase_add_test(tc, mp4_audio_duration_has_no_per_frame_truncation);
  tcase_add_test(tc, mp4_no_supported_tracks_writes_nothing_and_no_error);
  tcase_add_test(tc, mp4_multi_program_writes_two_audio_tracks);
  tcase_add_test(tc, mp4_pending_overflow_leaves_out_tracks_without_header);
  tcase_add_loop_test(tc, mp4_edge_case_streams_never_error_and_drop_unusable_frames, 0, EDGE_COUNT);
  tcase_add_loop_test(tc, mp4_video_header_parse_writes_sample_entry_and_samples, 0, (int)(sizeof video_cases / sizeof video_cases[0]));
  tcase_add_test(tc, mp4_video_with_reordering_writes_ctts_with_runs);
  tcase_add_test(tc, mp4_video_reorder_delay_is_skipped_by_edit_list);
  tcase_add_test(tc, mp4_video_without_reordering_omits_ctts);
  tcase_add_test(tc, mp4_selects_only_the_requested_audio_track);
  tcase_add_test(tc, mp4_unsupported_video_codec_writes_nothing_and_no_error);
  tcase_add_test(tc, mp4_checkpoint_makes_unclosed_file_indexed);
  tcase_add_test(tc, mp4_timestamp_splice_keeps_duration_of_content);
  tcase_add_test(tc, mp4_new_rejects_an_unseekable_output);
  tcase_add_test(tc, mp4_checkpoint_stays_exact_across_relayouts);
  tcase_add_test(tc, mp4_subtitle_gaps_become_empty_samples);
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
