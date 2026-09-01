/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/mux/pmt_filter.h"

typedef struct {
  unsigned pid;
  unsigned stream_type;
} pmt_es_spec_t;

static size_t build_pmt(unsigned char *out, unsigned pcr_pid, const unsigned char *prog_info, size_t prog_info_len, const pmt_es_spec_t *es, size_t n_es) {
  size_t o = 0;
  out[o++] = 0x02;
  out[o++] = 0;
  out[o++] = 0; /* section_length: pmt_filter_rewrite ignores it on input */
  out[o++] = 0x00;
  out[o++] = 0x01; /* program_number */
  out[o++] = 0xC1; /* version/current_next */
  out[o++] = 0x00; /* section_number */
  out[o++] = 0x00; /* last_section_number */
  out[o++] = (unsigned char)(0xE0 | ((pcr_pid >> 8) & 0x1F));
  out[o++] = (unsigned char)pcr_pid;
  out[o++] = (unsigned char)(0xF0 | ((prog_info_len >> 8) & 0x0F));
  out[o++] = (unsigned char)prog_info_len;
  memcpy(out + o, prog_info, prog_info_len);
  o += prog_info_len;
  for (size_t i = 0; i < n_es; i++) {
    out[o++] = (unsigned char)es[i].stream_type;
    out[o++] = (unsigned char)(0xE0 | ((es[i].pid >> 8) & 0x1F));
    out[o++] = (unsigned char)es[i].pid;
    out[o++] = 0xF0;
    out[o++] = 0x00; /* ES_info_length 0 */
  }
  memset(out + o, 0, 4); /* dummy CRC, ignored on input */
  o += 4;
  return o;
}

/* count/verify ES entries in a rewritten PMT's ES loop, pil at out[10..11] */
static size_t es_loop(const unsigned char *sec, size_t seclen, unsigned *pids, size_t max) {
  size_t pil = (((size_t)sec[10] & 0x0F) << 8) | sec[11];
  size_t i = 12 + pil, end = seclen - 4, n = 0;
  while (i + 5 <= end && n < max) {
    size_t esil = (((size_t)sec[i + 3] & 0x0F) << 8) | sec[i + 4];
    pids[n++] = (((unsigned)sec[i + 1] & 0x1F) << 8) | sec[i + 2];
    i += 5 + esil;
  }
  return n;
}

START_TEST(pmt_filter_rewrite_drops_one_es) {
  unsigned char in[64], out[64];
  pmt_es_spec_t es[] = {{0x0100, 0x1B}, {0x0101, 0x0F}};
  unsigned drop[] = {0x0101};
  unsigned pids[4];
  size_t inlen, outlen;

  inlen = build_pmt(in, 0x0100, NULL, 0, es, 2);
  outlen = pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out);

  ck_assert_uint_ne(outlen, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, outlen), 0u);
  ck_assert_uint_eq(es_loop(out, outlen, pids, 4), 1u);
  ck_assert_uint_eq(pids[0], 0x0100u);
}
END_TEST

START_TEST(pmt_filter_rewrite_drops_multiple_es) {
  unsigned char in[64], out[64];
  pmt_es_spec_t es[] = {{0x0100, 0x1B}, {0x0101, 0x0F}, {0x0102, 0x06}};
  unsigned drop[] = {0x0100, 0x0102};
  unsigned pids[4];
  size_t inlen, outlen;

  inlen = build_pmt(in, 0x0100, NULL, 0, es, 3);
  outlen = pmt_filter_rewrite(in, inlen, drop, 2, out, sizeof out);

  ck_assert_uint_ne(outlen, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, outlen), 0u);
  ck_assert_uint_eq(es_loop(out, outlen, pids, 4), 1u);
  ck_assert_uint_eq(pids[0], 0x0101u);
}
END_TEST

START_TEST(pmt_filter_rewrite_pid_not_present_leaves_es_loop_unchanged) {
  unsigned char in[64], out[64];
  pmt_es_spec_t es[] = {{0x0100, 0x1B}, {0x0101, 0x0F}};
  unsigned drop[] = {0x01FF};
  unsigned pids[4];
  size_t inlen, outlen;

  inlen = build_pmt(in, 0x0100, NULL, 0, es, 2);
  outlen = pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out);

  ck_assert_uint_ne(outlen, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, outlen), 0u);
  ck_assert_uint_eq(es_loop(out, outlen, pids, 4), 2u);
  ck_assert_uint_eq(pids[0], 0x0100u);
  ck_assert_uint_eq(pids[1], 0x0101u);
}
END_TEST

START_TEST(pmt_filter_rewrite_preserves_program_info) {
  unsigned char in[64], out[64];
  static const unsigned char prog_info[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  pmt_es_spec_t es[] = {{0x0100, 0x1B}};
  unsigned drop[] = {0x9999};
  size_t inlen, outlen, pil;

  inlen = build_pmt(in, 0x0100, prog_info, sizeof prog_info, es, 1);
  outlen = pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out);

  ck_assert_uint_ne(outlen, 0u);
  pil = (((size_t)out[10] & 0x0F) << 8) | out[11];
  ck_assert_uint_eq(pil, sizeof prog_info);
  ck_assert_mem_eq(out + 12, prog_info, sizeof prog_info);
}
END_TEST

START_TEST(pmt_filter_rewrite_rejects_short_section) {
  unsigned char in[15] = {0x02}, out[64];
  unsigned drop[] = {0};
  ck_assert_uint_eq(pmt_filter_rewrite(in, sizeof in, drop, 1, out, sizeof out), 0u);
}
END_TEST

START_TEST(pmt_filter_rewrite_rejects_wrong_table_id) {
  unsigned char in[64], out[64];
  pmt_es_spec_t es[] = {{0x0100, 0x1B}};
  unsigned drop[] = {0};
  size_t inlen = build_pmt(in, 0x0100, NULL, 0, es, 1);
  in[0] = 0x00; /* PAT, not PMT */
  ck_assert_uint_eq(pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out), 0u);
}
END_TEST

START_TEST(pmt_filter_rewrite_rejects_corrupt_program_info_length) {
  unsigned char in[64], out[64];
  pmt_es_spec_t es[] = {{0x0100, 0x1B}};
  unsigned drop[] = {0};
  size_t inlen = build_pmt(in, 0x0100, NULL, 0, es, 1);
  in[10] = 0x0F;
  in[11] = 0xFF; /* program_info_length far past end of section */
  ck_assert_uint_eq(pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out), 0u);
}
END_TEST

START_TEST(pmt_filter_rewrite_rejects_small_out_cap) {
  unsigned char in[64], out[14]; /* room for header, not the first ES entry */
  pmt_es_spec_t es[] = {{0x0100, 0x1B}, {0x0101, 0x0F}};
  unsigned drop[] = {0};
  size_t inlen = build_pmt(in, 0x0100, NULL, 0, es, 2);
  ck_assert_uint_eq(pmt_filter_rewrite(in, inlen, drop, 1, out, sizeof out), 0u);
}
END_TEST

START_TEST(pmt_filter_emit_packet_wraps_section) {
  static const unsigned char sec[] = {0x02, 0xB0, 0x09, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  unsigned char pkt[188];

  ck_assert_int_eq(pmt_filter_emit_packet(pkt, 0x0100, 0x07, sec, sizeof sec), 1);
  ck_assert_uint_eq(pkt[0], 0x47u);
  ck_assert_uint_eq(pkt[1], (unsigned char)(0x40 | 0x01));
  ck_assert_uint_eq(pkt[2], 0x00u);
  ck_assert_uint_eq(pkt[3], (unsigned char)(0x10 | 0x07));
  ck_assert_uint_eq(pkt[4], 0x00u);
  ck_assert_mem_eq(pkt + 5, sec, sizeof sec);
  ck_assert_uint_eq(pkt[5 + sizeof sec], 0xFFu);
  ck_assert_uint_eq(pkt[187], 0xFFu);
}
END_TEST

START_TEST(pmt_filter_emit_packet_rejects_oversized_section) {
  unsigned char sec[184] = {0};
  unsigned char pkt[188];
  ck_assert_int_eq(pmt_filter_emit_packet(pkt, 0x0100, 0, sec, sizeof sec), 0);
}
END_TEST

static Suite *pmt_filter_suite(void) {
  Suite *s = suite_create("pmt_filter");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pmt_filter_rewrite_drops_one_es);
  tcase_add_test(tc, pmt_filter_rewrite_drops_multiple_es);
  tcase_add_test(tc, pmt_filter_rewrite_pid_not_present_leaves_es_loop_unchanged);
  tcase_add_test(tc, pmt_filter_rewrite_preserves_program_info);
  tcase_add_test(tc, pmt_filter_rewrite_rejects_short_section);
  tcase_add_test(tc, pmt_filter_rewrite_rejects_wrong_table_id);
  tcase_add_test(tc, pmt_filter_rewrite_rejects_corrupt_program_info_length);
  tcase_add_test(tc, pmt_filter_rewrite_rejects_small_out_cap);
  tcase_add_test(tc, pmt_filter_emit_packet_wraps_section);
  tcase_add_test(tc, pmt_filter_emit_packet_rejects_oversized_section);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pmt_filter_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
