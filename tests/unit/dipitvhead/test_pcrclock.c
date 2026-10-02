/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <string.h>

#include "dipitvhead/mux/pcrclock.h"

START_TEST(clock_starts_at_the_pcr_byte_of_the_first_packet) {
  pcrclock_t c;
  pcrclock_init(&c, 10000000, 0);
  ck_assert_uint_eq((unsigned)pcrclock_at(&c, 0), 216u);
}
END_TEST

START_TEST(clock_advances_by_exact_packet_time) {
  pcrclock_t c;
  pcrclock_init(&c, 10000000, 1000);
  ck_assert_uint_eq((unsigned)(pcrclock_at(&c, 1000) - pcrclock_at(&c, 0)), 4060800u);
}
END_TEST

START_TEST(clock_does_not_drift_over_a_billion_packets) {
  pcrclock_t c;
  uint64_t n = 1000000000ULL;
  pcrclock_init(&c, 8000000, 0);
  ck_assert_uint_eq(pcr_sub(pcrclock_at(&c, n), pcrclock_at(&c, 0)), (n * 5076ULL) % PCR_MODULUS);
}
END_TEST

START_TEST(clock_wraps_at_the_pcr_modulus) {
  pcrclock_t c;
  pcrclock_init(&c, 10000000, PCR_MODULUS - 100);
  ck_assert_uint_eq(pcrclock_at(&c, 0), (PCR_MODULUS - 100 + 216) % PCR_MODULUS);
  ck_assert_uint_lt(pcrclock_at(&c, 0), 200u);
}
END_TEST

START_TEST(clock_base_is_reduced_modulo) {
  pcrclock_t c;
  pcrclock_init(&c, 10000000, PCR_MODULUS + 5);
  ck_assert_uint_eq(c.base27, 5u);
}
END_TEST

START_TEST(add_and_sub_wrap_around) {
  ck_assert_uint_eq(pcr_add(PCR_MODULUS - 1, 2), 1u);
  ck_assert_uint_eq(pcr_sub(1, 2), PCR_MODULUS - 1);
  ck_assert_uint_eq(pcr_sub(pcr_add(123456, 789), 789), 123456u);
}
END_TEST

START_TEST(built_packet_roundtrips_pcr_values) {
  static const uint64_t values[] = {0, 1, 299, 300, 301, 8589934591ULL * 300 + 299, 123456789012ULL};
  unsigned char pkt[188];
  uint64_t got;
  for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
    pcr_packet_build(pkt, 0x0100, 7, values[i]);
    ck_assert_int_eq(pcr_packet_read(pkt, &got), 1);
    ck_assert_uint_eq(got, values[i]);
  }
}
END_TEST

START_TEST(built_packet_is_adaptation_only_with_stuffing) {
  unsigned char pkt[188];
  pcr_packet_build(pkt, 0x1234, 9, 5000);
  ck_assert_uint_eq(pkt[0], 0x47u);
  ck_assert_uint_eq((unsigned)(((pkt[1] & 0x1F) << 8) | pkt[2]), 0x1234u);
  ck_assert_uint_eq(pkt[1] & 0x40, 0u);
  ck_assert_uint_eq(pkt[3], 0x29u);
  ck_assert_uint_eq(pkt[4], 183u);
  ck_assert_uint_eq(pkt[5], 0x10u);
  ck_assert_uint_eq(pkt[10] & 0x7E, 0x7Eu);
  for (int i = 12; i < 188; i++) ck_assert_uint_eq(pkt[i], 0xFFu);
}
END_TEST

START_TEST(known_pcr_encodes_to_known_bytes) {
  unsigned char pkt[188];
  pcr_packet_build(pkt, 0x0100, 0, 0x1FFFFFFFFULL * 300 + 299);
  ck_assert_uint_eq(pkt[6], 0xFFu);
  ck_assert_uint_eq(pkt[7], 0xFFu);
  ck_assert_uint_eq(pkt[8], 0xFFu);
  ck_assert_uint_eq(pkt[9], 0xFFu);
  ck_assert_uint_eq(pkt[10], 0xFFu & (0x80 | 0x7E | 0x01));
  ck_assert_uint_eq(pkt[11], 299u & 0xFFu);
}
END_TEST

START_TEST(write_keeps_flags_and_payload_untouched) {
  unsigned char pkt[188];
  unsigned char orig[188];
  uint64_t got;
  memset(pkt, 0xAA, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x41;
  pkt[2] = 0x00;
  pkt[3] = 0x35;
  pkt[4] = 7;
  pkt[5] = 0x50;
  pkt[10] = 0x7E;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pcr_packet_write(pkt, 90000 * 300ULL + 17), 0);
  ck_assert_int_eq(pcr_packet_read(pkt, &got), 1);
  ck_assert_uint_eq(got, 90000 * 300ULL + 17);
  ck_assert_mem_eq(pkt, orig, 6);
  ck_assert_mem_eq(pkt + 12, orig + 12, 176);
}
END_TEST

START_TEST(packets_without_pcr_are_left_alone) {
  unsigned char pkt[188];
  unsigned char orig[188];
  uint64_t got = 99;
  memset(pkt, 0x11, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pcr_packet_read(pkt, &got), 0);
  ck_assert_int_eq(pcr_packet_write(pkt, 1), -1);
  ck_assert_mem_eq(pkt, orig, sizeof pkt);
  pkt[3] = 0x30;
  pkt[4] = 7;
  pkt[5] = 0x00;
  ck_assert_int_eq(pcr_packet_read(pkt, &got), 0);
  ck_assert_int_eq(pcr_packet_write(pkt, 1), -1);
  pkt[5] = 0x10;
  pkt[4] = 6;
  ck_assert_int_eq(pcr_packet_read(pkt, &got), 0);
  ck_assert_int_eq(pcr_packet_write(pkt, 1), -1);
  ck_assert_uint_eq((unsigned)got, 99u);
}
END_TEST

START_TEST(write_reduces_values_beyond_the_modulus) {
  unsigned char pkt[188];
  uint64_t got;
  pcr_packet_build(pkt, 0x0100, 0, 0);
  ck_assert_int_eq(pcr_packet_write(pkt, PCR_MODULUS + 42), 0);
  ck_assert_int_eq(pcr_packet_read(pkt, &got), 1);
  ck_assert_uint_eq(got, 42u);
}
END_TEST

static Suite *pcrclock_suite(void) {
  Suite *s = suite_create("pcrclock");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, clock_starts_at_the_pcr_byte_of_the_first_packet);
  tcase_add_test(tc, clock_advances_by_exact_packet_time);
  tcase_add_test(tc, clock_does_not_drift_over_a_billion_packets);
  tcase_add_test(tc, clock_wraps_at_the_pcr_modulus);
  tcase_add_test(tc, clock_base_is_reduced_modulo);
  tcase_add_test(tc, add_and_sub_wrap_around);
  tcase_add_test(tc, built_packet_roundtrips_pcr_values);
  tcase_add_test(tc, built_packet_is_adaptation_only_with_stuffing);
  tcase_add_test(tc, known_pcr_encodes_to_known_bytes);
  tcase_add_test(tc, write_keeps_flags_and_payload_untouched);
  tcase_add_test(tc, packets_without_pcr_are_left_alone);
  tcase_add_test(tc, write_reduces_values_beyond_the_modulus);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pcrclock_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? 0 : 1;
}
