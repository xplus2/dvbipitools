/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipirec/filter/ts.h"
#include "lib/demux/crc32.h"

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

static void finish(unsigned char *sec, size_t n, unsigned char flags) {
  unsigned seclen = (unsigned)(n + 4 - 3);
  uint32_t crc;
  sec[1] = (unsigned char)(flags | ((seclen >> 8) & 0x0F));
  sec[2] = (unsigned char)seclen;
  crc = crc32_mpeg(sec, n);
  sec[n + 0] = (unsigned char)(crc >> 24);
  sec[n + 1] = (unsigned char)(crc >> 16);
  sec[n + 2] = (unsigned char)(crc >> 8);
  sec[n + 3] = (unsigned char)crc;
}

/* PAT with a program_number=0 (NIT) entry plus one real program */
static size_t build_pat_with_nit(unsigned char *out, unsigned tsid, unsigned nit_pid, unsigned prog_num, unsigned pmt_pid) {
  size_t n = 0;
  out[n++] = 0x00;
  n += 2;
  out[n++] = (unsigned char)(tsid >> 8);
  out[n++] = (unsigned char)tsid;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x00; /* program_number 0 = NIT */
  out[n++] = 0x00;
  out[n++] = (unsigned char)(0xE0 | ((nit_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)nit_pid;
  out[n++] = (unsigned char)(prog_num >> 8);
  out[n++] = (unsigned char)prog_num;
  out[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)pmt_pid;
  finish(out, n, 0xB0);
  return n + 4;
}

/* PMT: video(0x101, H264), audio1(0x102, AAC, "eng"), audio2(0x103, AAC, "spa"), subtitle(0x104) */
static size_t build_pmt_2audio_1sub(unsigned char *out, unsigned prog_num) {
  size_t n = 0;
  out[n++] = 0x02;
  n += 2;
  out[n++] = (unsigned char)(prog_num >> 8);
  out[n++] = (unsigned char)prog_num;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F); /* PCR = video */
  out[n++] = 0x01;
  out[n++] = 0xF0; /* program_info_length = 0 */
  out[n++] = 0x00;

  out[n++] = 0x1B; /* video H264 */
  out[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
  out[n++] = 0x01;
  out[n++] = 0xF0;
  out[n++] = 0x00;

  out[n++] = 0x0F; /* audio1 AAC */
  out[n++] = 0xE0 | ((0x0102 >> 8) & 0x1F);
  out[n++] = 0x02;
  out[n++] = 0xF0;
  out[n++] = 0x06;
  out[n++] = 0x0A;
  out[n++] = 4;
  out[n++] = 'e';
  out[n++] = 'n';
  out[n++] = 'g';
  out[n++] = 0x00;

  out[n++] = 0x0F; /* audio2 AAC */
  out[n++] = 0xE0 | ((0x0103 >> 8) & 0x1F);
  out[n++] = 0x03;
  out[n++] = 0xF0;
  out[n++] = 0x06;
  out[n++] = 0x0A;
  out[n++] = 4;
  out[n++] = 's';
  out[n++] = 'p';
  out[n++] = 'a';
  out[n++] = 0x00;

  out[n++] = 0x06; /* subtitle */
  out[n++] = 0xE0 | ((0x0104 >> 8) & 0x1F);
  out[n++] = 0x04;
  out[n++] = 0xF0;
  out[n++] = 0x0A; /* ES_info_length: 2 (tag+len) + 8 content bytes */
  out[n++] = 0x59;
  out[n++] = 8;
  out[n++] = 'e';
  out[n++] = 'n';
  out[n++] = 'g';
  out[n++] = 0x01; /* sub_type */
  out[n++] = 0x00; /* composition_page hi */
  out[n++] = 100;  /* composition_page lo */
  out[n++] = 0x00; /* ancillary_page hi */
  out[n++] = 200;  /* ancillary_page lo */

  finish(out, n, 0xB0);
  return n + 4;
}

/* PAT with two real programs, no NIT entry */
static size_t build_pat_2programs(unsigned char *out, unsigned tsid, unsigned prog1, unsigned pmt1, unsigned prog2, unsigned pmt2) {
  size_t n = 0;
  out[n++] = 0x00;
  n += 2;
  out[n++] = (unsigned char)(tsid >> 8);
  out[n++] = (unsigned char)tsid;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = (unsigned char)(prog1 >> 8);
  out[n++] = (unsigned char)prog1;
  out[n++] = (unsigned char)(0xE0 | ((pmt1 >> 8) & 0x1F));
  out[n++] = (unsigned char)pmt1;
  out[n++] = (unsigned char)(prog2 >> 8);
  out[n++] = (unsigned char)prog2;
  out[n++] = (unsigned char)(0xE0 | ((pmt2 >> 8) & 0x1F));
  out[n++] = (unsigned char)pmt2;
  finish(out, n, 0xB0);
  return n + 4;
}

/* PMT: one AAC audio ES at audio_pid, PCR = same pid */
static size_t build_pmt_1audio(unsigned char *out, unsigned prog_num, unsigned audio_pid) {
  size_t n = 0;
  out[n++] = 0x02;
  n += 2;
  out[n++] = (unsigned char)(prog_num >> 8);
  out[n++] = (unsigned char)prog_num;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = (unsigned char)(0xE0 | ((audio_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)audio_pid;
  out[n++] = 0xF0;
  out[n++] = 0x00;
  out[n++] = 0x0F; /* AAC */
  out[n++] = (unsigned char)(0xE0 | ((audio_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)audio_pid;
  out[n++] = 0xF0;
  out[n++] = 0x00;
  finish(out, n, 0xB0);
  return n + 4;
}

/* CAT with one CA_descriptor: ca_system_id, emm_pid */
static size_t build_cat_with_emm(unsigned char *out, unsigned emm_pid) {
  size_t n = 0;
  out[n++] = 0x01;
  n += 2;
  out[n++] = 0xFF; /* reserved, CAT has no table_id_extension of its own */
  out[n++] = 0xFF;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = 0x09; /* CA_descriptor */
  out[n++] = 4;
  out[n++] = 0x26;
  out[n++] = 0x02;
  out[n++] = (unsigned char)(0xE0 | ((emm_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)emm_pid;
  finish(out, n, 0xB0);
  return n + 4;
}

/* PMT: one AAC audio ES at audio_pid, with an ES-level CA_descriptor (ecm_pid) */
static size_t build_pmt_1audio_with_ecm(unsigned char *out, unsigned prog_num, unsigned audio_pid, unsigned ecm_pid) {
  size_t n = 0;
  out[n++] = 0x02;
  n += 2;
  out[n++] = (unsigned char)(prog_num >> 8);
  out[n++] = (unsigned char)prog_num;
  out[n++] = 0xC1;
  out[n++] = 0x00;
  out[n++] = 0x00;
  out[n++] = (unsigned char)(0xE0 | ((audio_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)audio_pid;
  out[n++] = 0xF0;
  out[n++] = 0x00;
  out[n++] = 0x0F; /* AAC */
  out[n++] = (unsigned char)(0xE0 | ((audio_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)audio_pid;
  out[n++] = 0xF0;
  out[n++] = 0x06;
  out[n++] = 0x09; /* CA_descriptor */
  out[n++] = 4;
  out[n++] = 0x26;
  out[n++] = 0x02;
  out[n++] = (unsigned char)(0xE0 | ((ecm_pid >> 8) & 0x1F));
  out[n++] = (unsigned char)ecm_pid;
  finish(out, n, 0xB0);
  return n + 4;
}

static void feed_pat_pmt(ts_filter_t *f) {
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;

  slen = build_pat_with_nit(sec, 0x1234, 0x0010, 101, 0x0100);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_pmt_2audio_1sub(sec, 101);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  ts_filter_packet(f, pkt, out);
}

static int filter_pid(ts_filter_t *f, unsigned pid) {
  unsigned char pkt[188], out[188];
  memset(pkt, 0xCD, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x00 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  return ts_filter_packet(f, pkt, out) != NULL;
}

START_TEST(ts_filter_pat_rewrite_drops_nit_only_program) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_DEFAULT);
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;
  psi_t *check;
  int count;
  const psi_program_t *progs;

  slen = build_pat_with_nit(sec, 0x1234, 0x0010, 101, 0x0100);
  wrap_ts_packet(pkt, 0x0000, sec, slen);

  {
    const unsigned char *r = ts_filter_packet(f, pkt, out);
    ck_assert_ptr_nonnull(r);
    ck_assert_uint_eq(r[0], 0x47u);
    check = psi_new();
    psi_feed(check, r);
  }
  ck_assert_int_eq(psi_have_pat(check), 1);
  progs = psi_pat_programs(check, &count);
  ck_assert_int_eq(count, 1); /* the NIT (program 0) entry was dropped */
  ck_assert_uint_eq(progs[0].program_number, 101u);

  psi_free(check);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_selects_single_audio_track) {
  ts_filter_t *f = ts_filter_new(0, 1, 0, 0, STRIP_DEFAULT); /* audio_all=0, track 1 */
  feed_pat_pmt(f);

  ck_assert_int_eq(filter_pid(f, 0x0101), 1); /* video always kept */
  ck_assert_int_eq(filter_pid(f, 0x0102), 1); /* audio track 1: kept */
  ck_assert_int_eq(filter_pid(f, 0x0103), 0); /* audio track 2: dropped */
  ck_assert_int_eq(filter_pid(f, 0x0104), 1); /* subtitle, strip_subs=0: kept */

  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_audio_all_and_strip_subs) {
  ts_filter_t *f = ts_filter_new(1, 0, 1, 0, STRIP_DEFAULT); /* audio_all=1, strip_subs=1 */
  feed_pat_pmt(f);

  ck_assert_int_eq(filter_pid(f, 0x0102), 1); /* both audio tracks kept */
  ck_assert_int_eq(filter_pid(f, 0x0103), 1);
  ck_assert_int_eq(filter_pid(f, 0x0104), 0); /* subtitle stripped */

  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_flags_bad_track_when_out_of_range) {
  ts_filter_t *f = ts_filter_new(0, 5, 0, 0, STRIP_DEFAULT); /* only 2 audio tracks exist */
  ck_assert_int_eq(ts_filter_bad_track(f), 0);
  feed_pat_pmt(f);
  ck_assert_int_eq(ts_filter_bad_track(f), 1);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_drops_null_packets) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_DEFAULT);
  feed_pat_pmt(f);
  ck_assert_int_eq(filter_pid(f, 0x1FFF), 0);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_preferred_pmt_pid_pins_non_first_candidate) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0x0200, STRIP_DEFAULT); /* pin program 102, not the first-resolving one */
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;

  slen = build_pat_2programs(sec, 0x1234, 101, 0x0100, 102, 0x0200);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_pmt_1audio(sec, 101, 0x0102);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_pmt_1audio(sec, 102, 0x0202);
  wrap_ts_packet(pkt, 0x0200, sec, slen);
  ts_filter_packet(f, pkt, out);

  ck_assert_uint_eq(psi_pmt_pid(ts_filter_psi(f)), 0x0200u);
  ck_assert_uint_eq(psi_program_number(ts_filter_psi(f)), 102u);
  ck_assert_int_eq(filter_pid(f, 0x0202), 1); /* program 102's own audio: kept */

  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_none_keeps_nit_and_null) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, 0); /* strip nothing */
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;
  psi_t *check;

  slen = build_pat_with_nit(sec, 0x1234, 0x0010, 101, 0x0100);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  {
    const unsigned char *r = ts_filter_packet(f, pkt, out);
    ck_assert_ptr_eq(r, pkt); /* not stripping NIT: passthrough, no copy */
    check = psi_new();
    psi_feed(check, r);
  }
  ck_assert_uint_eq(psi_nit_pid(check), 0x0010u); /* NIT entry still there */
  psi_free(check);

  ck_assert_int_eq(filter_pid(f, 0x1FFF), 1); /* NUL not stripped */
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_cat_ecm_emm_drops_ca_pids) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_CAT | STRIP_ECM | STRIP_EMM);
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;

  slen = build_pat_with_nit(sec, 0x1234, 0x0010, 101, 0x0100);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_cat_with_emm(sec, 0x0121);
  wrap_ts_packet(pkt, 0x0001, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_pmt_1audio_with_ecm(sec, 101, 0x0102, 0x0122);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  ts_filter_packet(f, pkt, out);

  ck_assert_int_eq(filter_pid(f, 0x0001), 0); /* CAT dropped */
  ck_assert_int_eq(filter_pid(f, 0x0121), 0); /* EMM dropped */
  ck_assert_int_eq(filter_pid(f, 0x0122), 0); /* ECM dropped */
  ck_assert_int_eq(filter_pid(f, 0x0102), 1); /* audio still kept */

  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_none_keeps_ca_pids) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, 0);
  unsigned char sec[256], pkt[188], out[188];
  size_t slen;

  slen = build_pat_with_nit(sec, 0x1234, 0x0010, 101, 0x0100);
  wrap_ts_packet(pkt, 0x0000, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_cat_with_emm(sec, 0x0121);
  wrap_ts_packet(pkt, 0x0001, sec, slen);
  ts_filter_packet(f, pkt, out);

  slen = build_pmt_1audio_with_ecm(sec, 101, 0x0102, 0x0122);
  wrap_ts_packet(pkt, 0x0100, sec, slen);
  ts_filter_packet(f, pkt, out);

  ck_assert_int_eq(filter_pid(f, 0x0001), 1); /* CAT kept */
  ck_assert_int_eq(filter_pid(f, 0x0121), 1); /* EMM kept */
  ck_assert_int_eq(filter_pid(f, 0x0122), 1); /* ECM kept */

  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_tdt_drops_pid_0x14) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_TDT);
  feed_pat_pmt(f);
  ck_assert_int_eq(filter_pid(f, 0x0014), 0);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_tot_also_drops_pid_0x14) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_TOT); /* TDT/TOT share a pid, not split */
  feed_pat_pmt(f);
  ck_assert_int_eq(filter_pid(f, 0x0014), 0);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_none_keeps_pid_0x14) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, 0);
  feed_pat_pmt(f);
  ck_assert_int_eq(filter_pid(f, 0x0014), 1);
  ts_filter_free(f);
}
END_TEST

START_TEST(ts_filter_strip_int_drops_by_table_id_and_latches) {
  ts_filter_t *f = ts_filter_new(1, 0, 0, 0, STRIP_INT);
  unsigned char sec[16], pkt[188], out[188];
  feed_pat_pmt(f);

  memset(sec, 0, sizeof sec);
  sec[0] = 0x4C; /* INT table_id */
  wrap_ts_packet(pkt, 0x0150, sec, 12);
  ck_assert_ptr_null(ts_filter_packet(f, pkt, out));

  pkt[1] &= (unsigned char)~0x40; /* continuation packet, no PUSI: still caught by latch */
  ck_assert_ptr_null(ts_filter_packet(f, pkt, out));

  ts_filter_free(f);
}
END_TEST

static Suite *ts_filter_suite(void) {
  Suite *s = suite_create("ts_filter");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ts_filter_pat_rewrite_drops_nit_only_program);
  tcase_add_test(tc, ts_filter_selects_single_audio_track);
  tcase_add_test(tc, ts_filter_audio_all_and_strip_subs);
  tcase_add_test(tc, ts_filter_flags_bad_track_when_out_of_range);
  tcase_add_test(tc, ts_filter_drops_null_packets);
  tcase_add_test(tc, ts_filter_preferred_pmt_pid_pins_non_first_candidate);
  tcase_add_test(tc, ts_filter_strip_none_keeps_nit_and_null);
  tcase_add_test(tc, ts_filter_strip_cat_ecm_emm_drops_ca_pids);
  tcase_add_test(tc, ts_filter_strip_none_keeps_ca_pids);
  tcase_add_test(tc, ts_filter_strip_tdt_drops_pid_0x14);
  tcase_add_test(tc, ts_filter_strip_tot_also_drops_pid_0x14);
  tcase_add_test(tc, ts_filter_strip_none_keeps_pid_0x14);
  tcase_add_test(tc, ts_filter_strip_int_drops_by_table_id_and_latches);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ts_filter_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
