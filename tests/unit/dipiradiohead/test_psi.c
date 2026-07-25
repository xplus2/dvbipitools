/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/mux/psi.h"
#include "lib/demux/crc32.h"
#include "lib/demux/psi.h"
#include "lib/mux/psi_build.h"

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

START_TEST(psi_build_pmt_round_trips_through_psi_feed) {
  unsigned char section[64], pkt[188];
  size_t slen;
  psi_t *p;
  const psi_es_t *es;
  int count;

  p = psi_new();

  /* psi_feed only recognizes a PMT pid once the PAT has pointed at it */
  slen = psi_build_pat(0x2222, 0, 101, 0x0100, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(p, pkt);
  ck_assert_int_eq(psi_have_pat(p), 1);

  slen = psi_build_pmt(3, 101, 0x0101, 0x0F, 0x0101, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);

  wrap_ts_packet(pkt, 0x0100, section, slen);
  psi_feed(p, pkt);

  ck_assert_int_eq(psi_have_pmt(p), 1);
  ck_assert_uint_eq(psi_program_number(p), 101u);
  ck_assert_uint_eq(psi_pcr_pid(p), 0x0101u);
  es = psi_es(p, &count);
  ck_assert_int_eq(count, 1);
  ck_assert_uint_eq(es[0].pid, 0x0101u);
  ck_assert_int_eq(es[0].cls, PID_AUDIO);
  ck_assert_int_eq(es[0].codec, CODEC_AAC);

  psi_free(p);
}
END_TEST

START_TEST(psi_build_pmt_rejects_small_cap) {
  unsigned char section[16];
  ck_assert_uint_eq(psi_build_pmt(0, 1, 0x100, 0x0F, 0x101, section, sizeof section), 0u);
}
END_TEST

START_TEST(psi_build_eit_has_valid_header_and_crc) {
  unsigned char section[512];
  size_t slen = psi_build_eit(2, 101, 1, 2, "Some Artist", "Some Title", 180, section, sizeof section);

  ck_assert_uint_ne(slen, 0u);
  ck_assert_uint_eq(crc32_mpeg(section, slen), 0u);
  ck_assert_uint_eq(section[0], 0x4Eu); /* EIT present/following table_id */
  ck_assert_uint_eq(((unsigned)section[3] << 8) | section[4], 101u); /* service_id */
  ck_assert_uint_eq(((unsigned)section[8] << 8) | section[9], 1u);   /* transport_stream_id */
  ck_assert_uint_eq(((unsigned)section[10] << 8) | section[11], 2u); /* original_network_id */

  /* duration 180s = 00:03:00, BCD-encoded at bytes 21,22,23 */
  ck_assert_uint_eq(section[21], 0x00u);
  ck_assert_uint_eq(section[22], 0x03u);
  ck_assert_uint_eq(section[23], 0x00u);

  ck_assert_ptr_nonnull(memmem(section, slen, "Some Artist Some Title", strlen("Some Artist Some Title")));
}
END_TEST

START_TEST(psi_build_eit_uses_title_only_when_no_artist) {
  unsigned char section[512];
  size_t slen = psi_build_eit(0, 1, 1, 1, "", "Just A Title", 0, section, sizeof section);
  ck_assert_uint_ne(slen, 0u);
  ck_assert_ptr_nonnull(memmem(section, slen, "Just A Title", strlen("Just A Title")));
}
END_TEST

START_TEST(psi_build_eit_rejects_small_cap) {
  unsigned char section[16];
  ck_assert_uint_eq(psi_build_eit(0, 1, 1, 1, "a", "b", 0, section, sizeof section), 0u);
}
END_TEST

static Suite *psi_suite(void) {
  Suite *s = suite_create("dipiradiohead_psi");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, psi_build_pmt_round_trips_through_psi_feed);
  tcase_add_test(tc, psi_build_pmt_rejects_small_cap);
  tcase_add_test(tc, psi_build_eit_has_valid_header_and_crc);
  tcase_add_test(tc, psi_build_eit_uses_title_only_when_no_artist);
  tcase_add_test(tc, psi_build_eit_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(psi_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
