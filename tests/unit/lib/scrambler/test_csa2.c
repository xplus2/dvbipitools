/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <dvbcsa/dvbcsa.h>

#include "lib/scrambler/csa2.h"
#include "lib/scrambler/scrambler.h"

/* round-trip CSA2 correctness against libdvbcsa = strongest check available */
static const unsigned char cw[CSA2_CW_LEN] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

START_TEST(csa2_encrypt_block_round_trips_with_library_decrypt) {
  unsigned char plain[64], buf[64];
  struct dvbcsa_key_s *dk;
  csa2_key_t *k;
  size_t i;

  for (i = 0; i < sizeof plain; i++)
    plain[i] = (unsigned char)i;
  memcpy(buf, plain, sizeof buf);

  k = csa2_key_new(cw);
  ck_assert_ptr_nonnull(k);
  csa2_encrypt_block(k, buf, sizeof buf);
  ck_assert_mem_ne(buf, plain, sizeof buf);
  csa2_key_free(k);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, buf, sizeof buf);
  ck_assert_mem_eq(buf, plain, sizeof buf);
  dvbcsa_key_free(dk);
}
END_TEST

START_TEST(scrambler_csa2_packet_rounds_to_8byte_blocks_and_round_trips) {
  unsigned char clear[188], pkt[188];
  struct dvbcsa_key_s *dk;
  scrambler_t *s;
  size_t i;

  /* no adaptation field: header 4 bytes, 184-byte payload (multiple of 8 already, no residual) */
  clear[0] = 0x47;
  clear[1] = 0x60;
  clear[2] = 0x80;
  clear[3] = 0x11; /* AFC=01 payload only, scrambling_control=00 */
  for (i = 4; i < 188; i++)
    clear[i] = (unsigned char)(i * 7);
  memcpy(pkt, clear, sizeof pkt);

  s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_ODD, cw, sizeof cw), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_ODD), 0);
  scrambler_free(s);

  /* header untouched except scrambling_control bits (top 2 of byte 3, now '11' odd) */
  ck_assert_uint_eq(pkt[0], clear[0]);
  ck_assert_uint_eq(pkt[1], clear[1]);
  ck_assert_uint_eq(pkt[2], clear[2]);
  ck_assert_uint_eq(pkt[3], (unsigned char)((clear[3] & 0x3F) | 0xC0));
  ck_assert_mem_ne(pkt + 4, clear + 4, 184); /* fully scrambled, 184 % 8 == 0 */

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, pkt + 4, 184);
  dvbcsa_key_free(dk);
  ck_assert_mem_eq(pkt + 4, clear + 4, 184);
}
END_TEST

START_TEST(scrambler_csa2_packet_leaves_residual_clear) {
  unsigned char clear[188], pkt[188];
  struct dvbcsa_key_s *dk;
  scrambler_t *s;
  size_t i;

  /* 5-byte adaptation field (1 length byte + 4): payload_off = 4+5 = 9, payload_size = 179, 179 % 8 == 3, so 3 trailing bytes stay clear */
  clear[0] = 0x47;
  clear[1] = 0x60;
  clear[2] = 0x80;
  clear[3] = 0x31; /* AFC=11 AF+payload */
  clear[4] = 0x04; /* AF length byte: 4 bytes follow */
  clear[5] = clear[6] = clear[7] = clear[8] = 0xFF;
  for (i = 9; i < 188; i++)
    clear[i] = (unsigned char)(i * 3);
  memcpy(pkt, clear, sizeof pkt);

  s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, sizeof cw), 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_EVEN), 0);
  scrambler_free(s);

  ck_assert_mem_eq(pkt, clear, 3);
  ck_assert_uint_eq(pkt[3], (unsigned char)((clear[3] & 0x3F) | 0x80));
  ck_assert_mem_eq(pkt + 4, clear + 4, 5);
  ck_assert_mem_ne(pkt + 9, clear + 9, 176);
  ck_assert_mem_eq(pkt + 9 + 176, clear + 9 + 176, 3);

  dk = dvbcsa_key_alloc();
  ck_assert_ptr_nonnull(dk);
  dvbcsa_key_set(cw, dk);
  dvbcsa_decrypt(dk, pkt + 9, 176);
  dvbcsa_key_free(dk);
  ck_assert_mem_eq(pkt + 9, clear + 9, 176);
}
END_TEST

START_TEST(scrambler_set_key_rejects_wrong_cw_len_for_csa2) {
  unsigned char cw16[16] = {0};
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw16, sizeof cw16), -1);
  scrambler_free(s);
}
END_TEST

static Suite *csa2_suite(void) {
  Suite *s = suite_create("csa2");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, csa2_encrypt_block_round_trips_with_library_decrypt);
  tcase_add_test(tc, scrambler_csa2_packet_rounds_to_8byte_blocks_and_round_trips);
  tcase_add_test(tc, scrambler_csa2_packet_leaves_residual_clear);
  tcase_add_test(tc, scrambler_set_key_rejects_wrong_cw_len_for_csa2);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(csa2_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
