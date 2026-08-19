/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/psi/psi.h"
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

START_TEST(psi_put_text_truncation_never_splits_a_utf8_sequence) {
  /* "e" + U+00E9 (0xC3 0xA9, 2 bytes) - cap leaves room for only 2 content
     bytes: exactly enough for "e" + one more, but not the whole 2-byte
     sequence, so it must be dropped whole rather than split */
  unsigned char b[3];
  size_t n = psi_put_text(b, sizeof b, "e\xC3\xA9");
  ck_assert_uint_eq(n, 2u); /* 0x15 + "e" only, 2-byte char dropped whole */
  ck_assert_uint_eq(b[0], 0x15);
  ck_assert_uint_eq(b[1], 'e');
}
END_TEST

START_TEST(psi_utf8_clamp_keeps_whole_sequences_only) {
  const char *s = "e\xC3\xA9\xE2\x80\x93z"; /* "e" + U+00E9 (2B) + U+2013 (3B) + "z" */
  size_t len = strlen(s);
  ck_assert_uint_eq(psi_utf8_clamp(s, len, len), len);   /* fits: unchanged */
  ck_assert_uint_eq(psi_utf8_clamp(s, len, 1), 1u);      /* just "e" */
  ck_assert_uint_eq(psi_utf8_clamp(s, len, 2), 1u);      /* mid 2-byte char: stop before it */
  ck_assert_uint_eq(psi_utf8_clamp(s, len, 3), 3u);      /* "e" + full 2-byte char */
  ck_assert_uint_eq(psi_utf8_clamp(s, len, 5), 3u);      /* mid 3-byte char: stop before it */
  ck_assert_uint_eq(psi_utf8_clamp(s, len, 6), 6u);      /* "e" + both chars */
}
END_TEST

/* wraps one PSI section into a single 188-byte TS packet, pusi=1, pointer=0 */
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

START_TEST(psi_build_pat_multi_round_trips_through_psi_feed) {
  unsigned char section[64], pkt[188];
  size_t slen;
  psi_t *p;
  int count, i;
  const psi_program_t *progs;
  psi_pat_entry_t entries[3] = {
      {101, 0x0100},
      {102, 0x0101},
      {103, 0x0102},
  };

  slen = psi_build_pat_multi(0x2222, 5, entries, 3, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 1);
  ck_assert_uint_eq(psi_transport_stream_id(p), 0x2222u);
  progs = psi_pat_programs(p, &count);
  ck_assert_int_eq(count, 3);
  for (i = 0; i < 3; i++) {
    ck_assert_uint_eq(progs[i].program_number, entries[i].program_number);
    ck_assert_uint_eq(progs[i].pmt_pid, entries[i].pmt_pid);
  }

  psi_free(p);
}
END_TEST

START_TEST(psi_build_pat_multi_rejects_small_cap) {
  unsigned char section[8];
  psi_pat_entry_t entries[3] = {{1, 0x100}, {2, 0x101}, {3, 0x102}};
  ck_assert_uint_eq(psi_build_pat_multi(1, 0, entries, 3, section, sizeof section), 0u);
}
END_TEST

START_TEST(psi_build_pat_multi_with_zero_programs_is_still_a_valid_empty_pat) {
  unsigned char section[16], pkt[188];
  size_t slen;
  psi_t *p;
  int count;

  slen = psi_build_pat_multi(1, 0, NULL, 0, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  p = psi_new();
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pat(p), 1);
  psi_pat_programs(p, &count);
  ck_assert_int_eq(count, 0);

  psi_free(p);
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

START_TEST(psi_build_sdt_multi_round_trips_through_psi_feed) {
  unsigned char sdtsec[128], patsec[64], sdtpkt[188], patpkt[188];
  size_t slen;
  psi_t *p;
  int count, i;
  const psi_multi_program_t *progs;
  psi_pat_entry_t entries[2] = {{101, 0x1000}, {102, 0x1001}};
  psi_sdt_entry_t services[2] = {
      {101, 0x01, "ProviderOne", "Channel One"},
      {102, 0x01, "ProviderTwo", "Channel Two"},
  };

  slen = psi_build_sdt_multi(1, 0x1111, 0x2222, services, 2, sdtsec, sizeof sdtsec);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(sdtsec, slen), 0u);

  p = psi_new();
  psi_enable_multi_program(p);
  slen = psi_build_pat_multi(0x1111, 0, entries, 2, patsec, sizeof patsec);
  wrap_ts_packet(patpkt, 0x0000, patsec, slen);
  psi_feed(p, patpkt);

  slen = psi_build_sdt_multi(1, 0x1111, 0x2222, services, 2, sdtsec, sizeof sdtsec);
  wrap_ts_packet(sdtpkt, 0x0011, sdtsec, slen);
  psi_feed(p, sdtpkt);

  ck_assert_int_eq(psi_have_sdt(p), 1);
  progs = psi_multi_programs(p, &count);
  ck_assert_int_eq(count, 2);
  for (i = 0; i < 2; i++) {
    ck_assert_str_eq(progs[i].provider_name, services[i].provider);
    ck_assert_str_eq(progs[i].service_name, services[i].service_name);
  }

  psi_free(p);
}
END_TEST

START_TEST(psi_build_sdt_multi_rejects_small_cap) {
  unsigned char section[8];
  psi_sdt_entry_t services[1] = {{1, 0x01, "P", "S"}};
  ck_assert_uint_eq(psi_build_sdt_multi(0, 1, 1, services, 1, section, sizeof section), 0u);
}
END_TEST

START_TEST(psi_build_sdt_multi_rejects_zero_services) {
  unsigned char section[64];
  ck_assert_uint_eq(psi_build_sdt_multi(0, 1, 1, NULL, 0, section, sizeof section), 0u);
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
  tcase_add_test(tc, psi_put_text_truncation_never_splits_a_utf8_sequence);
  tcase_add_test(tc, psi_utf8_clamp_keeps_whole_sequences_only);
  tcase_add_test(tc, psi_build_pat_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_pat_rejects_small_cap);
  tcase_add_test(tc, psi_build_pat_multi_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_pat_multi_rejects_small_cap);
  tcase_add_test(tc, psi_build_pat_multi_with_zero_programs_is_still_a_valid_empty_pat);
  tcase_add_test(tc, psi_build_nit_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_sdt_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_sdt_multi_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_sdt_multi_rejects_small_cap);
  tcase_add_test(tc, psi_build_sdt_multi_rejects_zero_services);
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
