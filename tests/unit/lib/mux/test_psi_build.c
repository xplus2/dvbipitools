/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi.h"
#include "lib/mux/psi_build.h"

START_TEST(psi_put16_writes_big_endian) {
  unsigned char b[2];
  psi_put16(b, 0xABCD);
  ck_assert_uint_eq(b[0], 0xAB);
  ck_assert_uint_eq(b[1], 0xCD);
}
END_TEST

START_TEST(psi_put_text_prefixes_charset_and_truncates) {
  unsigned char b[6];
  size_t n = psi_put_text(b, sizeof b, "hello");
  ck_assert_uint_eq(n, 6u);
  ck_assert_uint_eq(b[0], 0x15);
  ck_assert_mem_eq(b + 1, "hello", 5);

  n = psi_put_text(b, 4, "hello"); /* cap forces truncation to 3 chars + prefix */
  ck_assert_uint_eq(n, 4u);
  ck_assert_uint_eq(b[0], 0x15);
  ck_assert_mem_eq(b + 1, "hel", 3);
}
END_TEST

/* wraps one PSI section into a single 188-byte TS packet, pusi=1, pointer=0 */
static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  size_t i;
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

START_TEST(psi_build_pat_round_trips_through_psi_feed) {
  unsigned char section[32], pkt[188];
  size_t slen;
  psi_t *p;
  int count;
  const psi_program_t *progs;

  slen = psi_build_pat(0x2222, 3, 7, 0x0200, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u); /* self-verifying CRC */

  p = psi_new();
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 1);
  ck_assert_uint_eq(psi_transport_stream_id(p), 0x2222u);
  progs = psi_pat_programs(p, &count);
  ck_assert_int_eq(count, 1);
  ck_assert_uint_eq(progs[0].program_number, 7u);
  ck_assert_uint_eq(progs[0].pmt_pid, 0x0200u);

  psi_free(p);
}
END_TEST

START_TEST(psi_build_pat_rejects_small_cap) {
  unsigned char section[8];
  ck_assert_uint_eq(psi_build_pat(1, 0, 1, 0x100, section, sizeof section), 0u);
}
END_TEST

START_TEST(psi_build_nit_round_trips_through_psi_feed) {
  unsigned char section[64], pkt[188];
  size_t slen;
  psi_t *p;

  slen = psi_build_nit(1, 0x3333, 0x4444, "Test Network", section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  wrap_ts_packet(pkt, 0x0010, section, slen); /* NIT PID */
  psi_feed(p, pkt);

  ck_assert_str_eq(psi_network_name(p), "Test Network");
  psi_free(p);
}
END_TEST

START_TEST(psi_build_sdt_round_trips_through_psi_feed) {
  unsigned char section[64], pkt[188];
  size_t slen;
  psi_t *p;

  /* service_id 0 deliberately matches psi_t's default program_number(0) so parse_sdt's service_id==program_number gate passes without a PMT */
  slen = psi_build_sdt(1, 0x1111, 0x2222, 0, 0x01, "MyProvider", "MyService", section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  wrap_ts_packet(pkt, 0x0011, section, slen); /* SDT PID */
  psi_feed(p, pkt);

  ck_assert_str_eq(psi_provider_name(p), "MyProvider");
  ck_assert_str_eq(psi_service_name(p), "MyService");
  psi_free(p);
}
END_TEST

START_TEST(psi_build_cat_builds_valid_section) {
  static const unsigned char desc[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  unsigned char section[32];
  size_t slen;

  slen = psi_build_cat(2, desc, sizeof desc, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  ck_assert_uint_eq(section[0], 0x01);
  ck_assert_uint_eq(section[3], 0xFF);
  ck_assert_uint_eq(section[4], 0xFF);
  ck_assert_uint_eq(section[5], (unsigned char)(0xC0 | (2 << 1) | 0x01));
  ck_assert_uint_eq(section[6], 0x00);
  ck_assert_uint_eq(section[7], 0x00);
  ck_assert_mem_eq(section + 8, desc, sizeof desc);
}
END_TEST

START_TEST(psi_build_cat_rejects_small_cap) {
  unsigned char section[8];
  ck_assert_uint_eq(psi_build_cat(0, NULL, 0, section, sizeof section), 0u);
}
END_TEST

static Suite *psi_build_suite(void) {
  Suite *s = suite_create("psi_build");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, psi_put16_writes_big_endian);
  tcase_add_test(tc, psi_put_text_prefixes_charset_and_truncates);
  tcase_add_test(tc, psi_build_pat_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_pat_rejects_small_cap);
  tcase_add_test(tc, psi_build_nit_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_sdt_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_cat_builds_valid_section);
  tcase_add_test(tc, psi_build_cat_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(psi_build_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
