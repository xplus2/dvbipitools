/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/psi_section_asm.h"

/* table_id(1) + 2-byte length field (top nibble 0x7) + payload, no CRC - matches
   the plain MPEG-2 private sections ECM/EMM carry on the wire */
static size_t build_section(unsigned char table_id, const unsigned char *payload, size_t payload_len, unsigned char *out) {
  out[0] = table_id;
  out[1] = (unsigned char)(0x70 | ((payload_len >> 8) & 0x0F));
  out[2] = (unsigned char)(payload_len & 0xFF);
  memcpy(out + 3, payload, payload_len);
  return 3 + payload_len;
}

START_TEST(single_packet_section_completes_immediately) {
  psi_section_asm_t a;
  unsigned char payload[10];
  unsigned char sec[16];
  size_t seclen;
  unsigned char pl[187]; /* pointer_field + section, well within one packet */
  int i;

  memset(&a, 0, sizeof a);
  for (i = 0; i < 10; i++)
    payload[i] = (unsigned char)(i + 1);
  seclen = build_section(0x82, payload, sizeof payload, sec);

  pl[0] = 0x00; /* pointer_field: section starts immediately */
  memcpy(pl + 1, sec, seclen);

  ck_assert_int_eq(psi_section_asm_feed(&a, pl, 1 + seclen, 1), 1);
  ck_assert_uint_eq(a.expect, seclen);
  ck_assert_mem_eq(a.buf, sec, seclen);
}
END_TEST

START_TEST(multi_packet_section_reassembles) {
  psi_section_asm_t a;
  unsigned char payload[300];
  unsigned char sec[320];
  size_t seclen;
  unsigned char pl1[184], pl2[184];
  int i;
  size_t first_chunk, rest;

  memset(&a, 0, sizeof a);
  for (i = 0; i < (int)sizeof payload; i++)
    payload[i] = (unsigned char)(i * 3 + 7);
  seclen = build_section(0x82, payload, sizeof payload, sec);

  pl1[0] = 0x00;
  first_chunk = sizeof pl1 - 1;
  memcpy(pl1 + 1, sec, first_chunk);
  ck_assert_int_eq(psi_section_asm_feed(&a, pl1, sizeof pl1, 1), 0); /* not complete yet */

  rest = seclen - first_chunk;
  ck_assert_uint_le(rest, sizeof pl2);
  memcpy(pl2, sec + first_chunk, rest);
  ck_assert_int_eq(psi_section_asm_feed(&a, pl2, rest, 0), 1);

  ck_assert_uint_eq(a.expect, seclen);
  ck_assert_mem_eq(a.buf, sec, seclen);
}
END_TEST

START_TEST(pointer_field_skips_leftover_stuffing) {
  psi_section_asm_t a;
  unsigned char payload[5] = {1, 2, 3, 4, 5};
  unsigned char sec[8];
  size_t seclen = build_section(0x82, payload, sizeof payload, sec);
  unsigned char pl[30];

  memset(&a, 0, sizeof a);
  memset(pl, 0xFF, sizeof pl); /* leftover stuffing before the new section */
  pl[0] = 4;                   /* pointer_field: skip 4 stuffing bytes */
  memcpy(pl + 1 + 4, sec, seclen);

  ck_assert_int_eq(psi_section_asm_feed(&a, pl, 1 + 4 + seclen, 1), 1);
  ck_assert_uint_eq(a.expect, seclen);
  ck_assert_mem_eq(a.buf, sec, seclen);
}
END_TEST

START_TEST(feed_without_prior_pusi_is_ignored) {
  psi_section_asm_t a;
  unsigned char pl[20] = {0};

  memset(&a, 0, sizeof a);
  ck_assert_int_eq(psi_section_asm_feed(&a, pl, sizeof pl, 0), 0);
}
END_TEST

START_TEST(oversized_section_length_resets_without_crash) {
  psi_section_asm_t a;
  unsigned char pl[10];

  memset(&a, 0, sizeof a);
  pl[0] = 0x00;
  pl[1] = 0x82;
  pl[2] = 0x7F; /* section_length top nibble 0x7 -> 0xFFF, far larger than PSI_SECTION_ASM_BUF_LEN */
  pl[3] = 0xFF;

  ck_assert_int_eq(psi_section_asm_feed(&a, pl, sizeof pl, 1), 0);
  /* recovers cleanly on the next section */
  {
    unsigned char payload[4] = {9, 9, 9, 9};
    unsigned char sec[8];
    size_t seclen = build_section(0x82, payload, sizeof payload, sec);
    unsigned char pl2[16];

    pl2[0] = 0x00;
    memcpy(pl2 + 1, sec, seclen);
    ck_assert_int_eq(psi_section_asm_feed(&a, pl2, 1 + seclen, 1), 1);
    ck_assert_uint_eq(a.expect, seclen);
    ck_assert_mem_eq(a.buf, sec, seclen);
  }
}
END_TEST

START_TEST(second_section_after_first_resets_state) {
  psi_section_asm_t a;
  unsigned char payload_a[4] = {1, 1, 1, 1};
  unsigned char payload_b[6] = {2, 2, 2, 2, 2, 2};
  unsigned char sec_a[8], sec_b[10];
  size_t len_a = build_section(0x82, payload_a, sizeof payload_a, sec_a);
  size_t len_b = build_section(0x82, payload_b, sizeof payload_b, sec_b);
  unsigned char pl[20];

  memset(&a, 0, sizeof a);

  pl[0] = 0x00;
  memcpy(pl + 1, sec_a, len_a);
  ck_assert_int_eq(psi_section_asm_feed(&a, pl, 1 + len_a, 1), 1);
  ck_assert_uint_eq(a.expect, len_a);
  ck_assert_mem_eq(a.buf, sec_a, len_a);

  pl[0] = 0x00;
  memcpy(pl + 1, sec_b, len_b);
  ck_assert_int_eq(psi_section_asm_feed(&a, pl, 1 + len_b, 1), 1);
  ck_assert_uint_eq(a.expect, len_b);
  ck_assert_mem_eq(a.buf, sec_b, len_b);
}
END_TEST

static Suite *psi_section_asm_suite(void) {
  Suite *s = suite_create("psi_section_asm");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_packet_section_completes_immediately);
  tcase_add_test(tc, multi_packet_section_reassembles);
  tcase_add_test(tc, pointer_field_skips_leftover_stuffing);
  tcase_add_test(tc, feed_without_prior_pusi_is_ignored);
  tcase_add_test(tc, oversized_section_length_resets_without_crash);
  tcase_add_test(tc, second_section_after_first_resets_state);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(psi_section_asm_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
