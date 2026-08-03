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
#include "lib/mux/mkv.h"
#include "lib/mux/psi_build.h"

/* Only audio path (video needs H.264/HEVC SPS bitstreams). mkv.c discovers PAT/PMT/SDT itself via mkv_feed */
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
  payload[0] = 0x00; /* pointer_field */
  memcpy(payload + 1, section, slen);
  wrap_ts_packet(pkt, pid, 1, payload, slen + 1);
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

static mkv_opts_t base_cfg(void) {
  mkv_opts_t opts;
  memset(&opts, 0, sizeof opts);
  opts.audio_all = 1;
  opts.app_name = "test";
  return opts;
}

/* feeds PAT (program 101 -> PMT pid 0x100), a 1-audio-ES PMT (AAC, pid 0x101),
   and an SDT for program 101 (mkv doesn't wait ~2s real time for one) */
static void feed_discovery(mkv_t *m) {
  unsigned char sec[256], pkt[188];
  size_t slen;

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  mkv_feed(m, pkt);

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
  mkv_feed(m, pkt);

  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  mkv_feed(m, pkt);
}

/* like feed_discovery, but two AAC ES (pid 0x101, 0x102):
   start(), then wait for both headers, anchor t0 on a real queued timestamp
   instead of the immediate first-frame default=0 */
static void feed_discovery_two_audio(mkv_t *m) {
  unsigned char sec[256], pkt[188];
  size_t slen;

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  mkv_feed(m, pkt);

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
    body[n++] = 0x0F; /* AAC, pid 0x101 */
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
    body[n++] = 0x0F; /* AAC, pid 0x102 */
    body[n++] = 0xE0 | ((0x0102 >> 8) & 0x1F);
    body[n++] = 0x02;
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
  mkv_feed(m, pkt);

  slen = psi_build_sdt(0, 0x1234, 2, 101, 0x01, "Provider", "Service", sec, sizeof sec);
  wrap_section_packet(pkt, 0x0011, sec, slen);
  mkv_feed(m, pkt);
}

/* fix: pid 0x101 crosses 33-bit PTS wrap at ~26.5h (?) after t0 is anchored near wrap point: re-base to avoid dropping it.
   pes.c only delivers a PID's buffered PES pl once a _next_ PES for the same PID starts (or any close flush).
   any "real" frame below needs a follow-up.  ADTS frame sizing == TS' 184 B -> wrap_ts_packet's 0xff never leaks into ES */
START_TEST(mkv_pts_wraparound_is_rebased_not_dropped) {
  char path[] = "/tmp/dvbipitools_test_mkv_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mkv_opts_t cfg = base_cfg();
  mkv_t *m;
  unsigned char adts[256], pes[256], pkt[188];
  size_t alen, plen;
  uint64_t raw1, raw2;
  FILE *f;
  unsigned char *buf;
  long fsize;
  static const unsigned char marker[16] = {0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD};

  ck_assert_int_ge(fd, 0);
  m = mkv_new(fd, &cfg, 0, &bytes);
  ck_assert_ptr_nonnull(m);

  feed_discovery_two_audio(m);
  raw1 = 0x200000000ULL - 900000; /* 10000 ms before the wrap */
  raw2 = 90000;                   /* 1000 ms after the wrap */

  /* pid 0x101 frame 1 (raw1): buffered, undelivered */
  alen = build_adts_frame(adts, 170);
  plen = build_pes_with_pts(pes, raw1, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mkv_feed(m, pkt);

  /* pid 0x102 frame 1 (raw1): buffered, undelivered */
  alen = build_adts_frame(adts, 170);
  plen = build_pes_with_pts(pes, raw1, adts, alen);
  wrap_ts_packet(pkt, 0x0102, 1, pes, plen);
  mkv_feed(m, pkt);

  /* pid 0x101 frame 2 (raw2, past wrap, marked): delivers frame 1 (raw1) first -> pends it (pid 0x102 not hdr_parsed yet)
     frame 2 itself is buffered, undelivered */
  alen = build_adts_frame(adts, 170);
  memset(adts + 7, 0xCD, alen - 7);
  plen = build_pes_with_pts(pes, raw2, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mkv_feed(m, pkt);

  /* pid 0x102 frame 2 (dummy): delivers pid 0x102 frame 1 (raw1) -> both tracks hdr_parsed -> start(), t0 anchored at raw1/90 */
  alen = build_adts_frame(adts, 170);
  plen = build_pes_with_pts(pes, raw1, adts, alen);
  wrap_ts_packet(pkt, 0x0102, 1, pes, plen);
  mkv_feed(m, pkt);

  ck_assert_int_eq(mkv_error(m), 0);
  /* mkv_close flushes pid 0x101's buffered frame 2 (raw2): m->started already true. post-start wraparound here.
     "it's tricky to rock a rhyme, to rock a rhyme that's right on time" */
  mkv_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  ck_assert(fsize > 0);
  buf = malloc((size_t)fsize);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);

  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, marker, sizeof marker));
  free(buf);
  unlink(path);
}
END_TEST

START_TEST(mkv_writes_a_valid_container_for_audio_only) {
  char path[] = "/tmp/dvbipitools_test_mkv_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mkv_opts_t cfg = base_cfg();
  mkv_t *m;
  unsigned char adts[64], pes[128], pkt[188];
  size_t alen, plen;
  FILE *f;
  unsigned char *buf;
  long fsize;

  ck_assert_int_ge(fd, 0);
  m = mkv_new(fd, &cfg, 0 /* audio-only, mka */, &bytes);
  ck_assert_ptr_nonnull(m);

  feed_discovery(m);
  alen = build_adts_frame(adts, 50);
  plen = build_pes_with_pts(pes, 90000, adts, alen);
  wrap_ts_packet(pkt, 0x0101, 1, pes, plen);
  mkv_feed(m, pkt);
  ck_assert_int_eq(mkv_error(m), 0);
  mkv_close(m);
  close(fd);

  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  rewind(f);
  ck_assert(fsize > 0);
  buf = malloc((size_t)fsize);
  ck_assert_uint_eq(fread(buf, 1, (size_t)fsize, f), (size_t)fsize);
  fclose(f);

  ck_assert(fsize >= 4);
  ck_assert_uint_eq(buf[0], 0x1Au); /* EBML header id 0x1A45DFA3 */
  ck_assert_uint_eq(buf[1], 0x45u);
  ck_assert_uint_eq(buf[2], 0xDFu);
  ck_assert_uint_eq(buf[3], 0xA3u);
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "matroska", 8));
  ck_assert_ptr_nonnull(memmem(buf, (size_t)fsize, "A_AAC", 5));
  free(buf);
  unlink(path);
}
END_TEST

START_TEST(mkv_no_supported_tracks_writes_nothing_and_no_error) {
  char path[] = "/tmp/dvbipitools_test_mkv_XXXXXX";
  int fd = mkstemp(path);
  unsigned long long bytes = 0;
  mkv_opts_t cfg = base_cfg();
  mkv_t *m;
  unsigned char sec[256], pkt[188];
  size_t slen;
  long fsize;
  FILE *f;

  cfg.audio_all = 0;
  cfg.audio_track = 99; /* no such track: the one AAC ES won't be selected */

  ck_assert_int_ge(fd, 0);
  m = mkv_new(fd, &cfg, 0, &bytes);

  /* PAT+PMT only: no track selected, so mkv never reaches the SDT wait */
  slen = psi_build_pat(0x1234, 0, 101, 0x0100, sec, sizeof sec);
  wrap_section_packet(pkt, 0x0000, sec, slen);
  mkv_feed(m, pkt);

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
  mkv_feed(m, pkt);
  ck_assert_int_eq(mkv_error(m), 0);
  mkv_close(m);
  close(fd);
  f = fopen(path, "rb");
  fseek(f, 0, SEEK_END);
  fsize = ftell(f);
  fclose(f);
  ck_assert_int_eq(fsize, 0);

  unlink(path);
}
END_TEST

static Suite *mkv_suite(void) {
  Suite *s = suite_create("mkv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mkv_writes_a_valid_container_for_audio_only);
  tcase_add_test(tc, mkv_no_supported_tracks_writes_nothing_and_no_error);
  tcase_add_test(tc, mkv_pts_wraparound_is_rebased_not_dropped);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mkv_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
