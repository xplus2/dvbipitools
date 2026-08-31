/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <string.h>

#include "dipixy/ts/pidfilter.h"
#include "dipixy/ts/rawaudio.h"
#include "lib/demux/crc32.h"

static size_t build_pat(unsigned char *out, unsigned tsid, unsigned prog_num, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(tsid >> 8);
  body[n++] = (unsigned char)tsid;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;

  hdr = n + 4;
  out[0] = 0x00;
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

/* one-program PMT, two MPEG-1 audio ES (stream_type 0x03), no video */
static size_t build_pmt_2audio(unsigned char *out, unsigned prog_num, unsigned pid1, unsigned pid2) {
  unsigned char body[32];
  size_t n = 0, hdr, crc_at;
  uint32_t crc;

  body[n++] = (unsigned char)(prog_num >> 8);
  body[n++] = (unsigned char)prog_num;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pid1 >> 8) & 0x1F));
  body[n++] = (unsigned char)pid1;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x03;
  body[n++] = (unsigned char)(0xE0 | ((pid1 >> 8) & 0x1F));
  body[n++] = (unsigned char)pid1;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x03;
  body[n++] = (unsigned char)(0xE0 | ((pid2 >> 8) & 0x1F));
  body[n++] = (unsigned char)pid2;
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

static void wrap_psi_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, const unsigned char *section,
                             size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

/* one TS packet carrying a PES start (00 00 01, no PTS/DTS) + payload */
static void wrap_pes_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, const unsigned char *payload,
                             size_t plen) {
  unsigned char hdr[9] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00};
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
  memcpy(pkt + 4, hdr, sizeof hdr);
  memcpy(pkt + 4 + sizeof hdr, payload, plen);
  for (size_t i = 4 + sizeof hdr + plen; i < 188; i++)
    pkt[i] = 0xFF;
}

typedef struct {
  int calls;
  unsigned char last[32];
  size_t last_len;
} emit_ctx_t;

static void emit_cb(void *vctx, const unsigned char *data, size_t len) {
  emit_ctx_t *c = vctx;
  c->calls++;
  memcpy(c->last, data, len < sizeof c->last ? len : sizeof c->last);
  c->last_len = len;
}

static void feed_pat_pmt(rawaudio_demux_t *d, unsigned pid1, unsigned pid2) {
  unsigned char section[64], pkt[188];
  size_t slen;

  slen = build_pat(section, 0x1, 1, 0x0100);
  wrap_psi_packet(pkt, 0x0000, 0, section, slen);
  rawaudio_demux_feed(d, pkt);

  slen = build_pmt_2audio(section, 1, pid1, pid2);
  wrap_psi_packet(pkt, 0x0100, 0, section, slen);
  rawaudio_demux_feed(d, pkt);
}

START_TEST(rawaudio_locks_lowest_audio_es_no_filter) {
  pid_filter_t f;
  emit_ctx_t ctx;
  rawaudio_demux_t *d;
  unsigned char pkt[188];

  memset(&f, 0, sizeof f);
  memset(&ctx, 0, sizeof ctx);
  d = rawaudio_demux_new(0, &f, emit_cb, &ctx);
  ck_assert_ptr_nonnull(d);

  feed_pat_pmt(d, 0x101, 0x102);

  wrap_pes_packet(pkt, 0x101, 0, (const unsigned char *)"AAAA", 4);
  rawaudio_demux_feed(d, pkt);
  ck_assert_int_eq(ctx.calls, 0); /* held until next PUSI on this pid */

  wrap_pes_packet(pkt, 0x101, 1, (const unsigned char *)"BB", 2);
  rawaudio_demux_feed(d, pkt);
  ck_assert_int_eq(ctx.calls, 1);
  ck_assert_uint_eq(ctx.last_len, 175); /* 184 B payload - 9 B PES header, rest 0xFF filler */
  ck_assert_mem_eq(ctx.last, "AAAA", 4);

  wrap_pes_packet(pkt, 0x102, 0, (const unsigned char *)"ZZ", 2);
  rawaudio_demux_feed(d, pkt);
  ck_assert_int_eq(ctx.calls, 1); /* untracked pid: no effect */

  rawaudio_demux_free(d);
}
END_TEST

START_TEST(rawaudio_skips_filtered_lowest_es) {
  pid_filter_t f;
  emit_ctx_t ctx;
  rawaudio_demux_t *d;
  unsigned char pkt[188];

  pid_filter_parse("257", &f); /* 0x101 */
  memset(&ctx, 0, sizeof ctx);
  d = rawaudio_demux_new(0, &f, emit_cb, &ctx);
  ck_assert_ptr_nonnull(d);

  feed_pat_pmt(d, 0x101, 0x102);

  wrap_pes_packet(pkt, 0x101, 0, (const unsigned char *)"XX", 2);
  rawaudio_demux_feed(d, pkt);
  wrap_pes_packet(pkt, 0x102, 0, (const unsigned char *)"YYY", 3);
  rawaudio_demux_feed(d, pkt);
  wrap_pes_packet(pkt, 0x102, 1, (const unsigned char *)"Q", 1);
  rawaudio_demux_feed(d, pkt);

  ck_assert_int_eq(ctx.calls, 1);
  ck_assert_uint_eq(ctx.last_len, 175);
  ck_assert_mem_eq(ctx.last, "YYY", 3);

  rawaudio_demux_free(d);
}
END_TEST

START_TEST(rawaudio_no_audio_left_never_emits) {
  pid_filter_t f;
  emit_ctx_t ctx;
  rawaudio_demux_t *d;
  unsigned char pkt[188];

  pid_filter_parse("257,258", &f); /* 0x101, 0x102 */
  memset(&ctx, 0, sizeof ctx);
  d = rawaudio_demux_new(0, &f, emit_cb, &ctx);
  ck_assert_ptr_nonnull(d);
  feed_pat_pmt(d, 0x101, 0x102);
  wrap_pes_packet(pkt, 0x101, 0, (const unsigned char *)"X", 1);
  rawaudio_demux_feed(d, pkt);
  wrap_pes_packet(pkt, 0x102, 0, (const unsigned char *)"Y", 1);
  rawaudio_demux_feed(d, pkt);
  ck_assert_int_eq(ctx.calls, 0);

  rawaudio_demux_free(d);
}
END_TEST

static Suite *rawaudio_suite(void) {
  Suite *s = suite_create("dipixy_rawaudio");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rawaudio_locks_lowest_audio_es_no_filter);
  tcase_add_test(tc, rawaudio_skips_filtered_lowest_es);
  tcase_add_test(tc, rawaudio_no_audio_left_never_emits);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  int failed;
  Suite *s = rawaudio_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed ? 1 : 0;
}
