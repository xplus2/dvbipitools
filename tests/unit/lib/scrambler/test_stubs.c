/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/scrambler/cissa.h"
#include "lib/scrambler/csa2.h"
#include "lib/scrambler/scrambler.h"

static unsigned emit_count;

static void count_emit(void *ctx, const unsigned char pkt[188]) {
  (void)ctx;
  (void)pkt;
  emit_count++;
}

static void make_pkt(unsigned char pkt[188], unsigned ctrl) {
  memset(pkt, 0xA5, 188);
  pkt[0] = 0x47;
  pkt[3] = (unsigned char)((ctrl << 6) | 0x10);
}

START_TEST(cissa_key_new_fails) {
  unsigned char cw[CISSA_CW_LEN] = {0};
  ck_assert_ptr_null(cissa_key_new(cw));
}
END_TEST

START_TEST(cissa_block_ops_fail_and_free_accepts_null) {
  unsigned char buf[32];
  memset(buf, 0x5A, sizeof buf);
  ck_assert_int_eq(cissa_encrypt_block(NULL, buf, sizeof buf), -1);
  ck_assert_int_eq(cissa_decrypt_block(NULL, buf, sizeof buf), -1);
  cissa_key_free(NULL);
}
END_TEST

START_TEST(csa2_key_new_fails) {
  unsigned char cw[CSA2_CW_LEN] = {0};
  ck_assert_ptr_null(csa2_key_new(cw));
}
END_TEST

START_TEST(csa2_block_ops_leave_data_untouched) {
  unsigned char buf[32];
  unsigned char ref[32];
  memset(buf, 0x5A, sizeof buf);
  memcpy(ref, buf, sizeof ref);
  csa2_encrypt_block(NULL, buf, sizeof buf);
  csa2_decrypt_block(NULL, buf, sizeof buf);
  csa2_key_free(NULL);
  ck_assert_mem_eq(buf, ref, sizeof buf);
}
END_TEST

START_TEST(csa2_batch_reports_no_capacity_and_leaves_data_untouched) {
  unsigned char buf[32];
  unsigned char ref[32];
  csa2_batch_entry_t e = {buf, sizeof buf};
  memset(buf, 0x5A, sizeof buf);
  memcpy(ref, buf, sizeof ref);
  ck_assert_uint_eq(csa2_batch_size(), 0);
  csa2_encrypt_batch(NULL, &e, 1);
  csa2_decrypt_batch(NULL, &e, 1);
  ck_assert_mem_eq(buf, ref, sizeof buf);
}
END_TEST

START_TEST(scrambler_set_key_fails_for_both_algos) {
  unsigned char cw[CISSA_CW_LEN] = {0};
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CISSA);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_EVEN, cw, CISSA_CW_LEN, NULL, NULL), -1);
  scrambler_free(s);

  s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(scrambler_set_key(s, SCRAMBLE_PARITY_ODD, cw, CSA2_CW_LEN, NULL, NULL), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_packet_ops_refuse_without_key) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);

  make_pkt(pkt, 0);
  ck_assert_int_eq(scrambler_encrypt_packet(s, pkt, SCRAMBLE_PARITY_EVEN), -1);
  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), 0);

  make_pkt(pkt, 2);
  ck_assert_int_eq(scrambler_decrypt_packet(s, pkt), -1);
  scrambler_free(s);
}
END_TEST

START_TEST(scrambler_queued_ops_emit_only_clear_packets) {
  unsigned char pkt[188];
  scrambler_t *s = scrambler_new(SCRAMBLE_ALGO_CSA2);
  ck_assert_ptr_nonnull(s);
  emit_count = 0;

  make_pkt(pkt, 0);
  ck_assert_int_eq(scrambler_encrypt_packet_queued(s, pkt, SCRAMBLE_PARITY_EVEN, count_emit, NULL), -1);
  ck_assert_int_eq(scrambler_decrypt_packet_queued(s, pkt, count_emit, NULL), 0);
  ck_assert_uint_eq(emit_count, 1);

  make_pkt(pkt, 3);
  ck_assert_int_eq(scrambler_decrypt_packet_queued(s, pkt, count_emit, NULL), -1);
  ck_assert_uint_eq(emit_count, 1);

  make_pkt(pkt, 0);
  scrambler_passthrough_queued(s, pkt, count_emit, NULL);
  scrambler_flush(s, count_emit, NULL);
  ck_assert_uint_eq(emit_count, 2);
  scrambler_free(s);
}
END_TEST

static Suite *stubs_suite(void) {
  Suite *s = suite_create("scrambler_stubs");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, cissa_key_new_fails);
  tcase_add_test(tc, cissa_block_ops_fail_and_free_accepts_null);
  tcase_add_test(tc, csa2_key_new_fails);
  tcase_add_test(tc, csa2_block_ops_leave_data_untouched);
  tcase_add_test(tc, csa2_batch_reports_no_capacity_and_leaves_data_untouched);
  tcase_add_test(tc, scrambler_set_key_fails_for_both_algos);
  tcase_add_test(tc, scrambler_packet_ops_refuse_without_key);
  tcase_add_test(tc, scrambler_queued_ops_emit_only_clear_packets);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(stubs_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
