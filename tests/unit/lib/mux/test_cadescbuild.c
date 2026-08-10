/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/mux/cadescbuild.h"

START_TEST(cadescbuild_ca_descriptor_builds_expected_bytes) {
  unsigned char out[16];
  size_t n = cadescbuild_ca_descriptor(0x4A75, 0x0020, out, sizeof out);
  static const unsigned char expect[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  ck_assert_uint_eq(n, sizeof expect);
  ck_assert_mem_eq(out, expect, sizeof expect);
}
END_TEST

START_TEST(cadescbuild_ca_descriptor_rejects_small_cap) {
  unsigned char out[5];
  ck_assert_uint_eq(cadescbuild_ca_descriptor(0x4A75, 0x0020, out, sizeof out), 0u);
}
END_TEST

START_TEST(cadescbuild_ca_descriptor_priv_appends_private_data) {
  unsigned char out[16];
  static const unsigned char priv[] = {0x80, 0x04, 0x12, 0x34, 0x56, 0x78};
  size_t n = cadescbuild_ca_descriptor_priv(0x2610, 0x1FFF, priv, sizeof priv, out, sizeof out);
  static const unsigned char expect[] = {0x09, 10, 0x26, 0x10, 0xFF, 0xFF, 0x80, 0x04, 0x12, 0x34, 0x56, 0x78};
  ck_assert_uint_eq(n, sizeof expect);
  ck_assert_mem_eq(out, expect, sizeof expect);
}
END_TEST

START_TEST(cadescbuild_ca_descriptor_priv_null_matches_plain) {
  unsigned char out[16];
  size_t n = cadescbuild_ca_descriptor_priv(0x4A75, 0x0020, NULL, 0, out, sizeof out);
  static const unsigned char expect[] = {0x09, 4, 0x4A, 0x75, 0xE0, 0x20};
  ck_assert_uint_eq(n, sizeof expect);
  ck_assert_mem_eq(out, expect, sizeof expect);
}
END_TEST

START_TEST(cadescbuild_ca_descriptor_priv_rejects_small_cap) {
  unsigned char out[8];
  static const unsigned char priv[] = {0x80, 0x04, 0x12, 0x34, 0x56, 0x78};
  ck_assert_uint_eq(cadescbuild_ca_descriptor_priv(0x2610, 0x1FFF, priv, sizeof priv, out, sizeof out), 0u);
}
END_TEST

START_TEST(cadescbuild_scrambling_descriptor_builds_expected_bytes) {
  unsigned char out[8];
  size_t n = cadescbuild_scrambling_descriptor(CADESC_SCRAMBLING_MODE_CISSA, out, sizeof out);
  static const unsigned char expect[] = {0x65, 1, 0x10};
  ck_assert_uint_eq(n, sizeof expect);
  ck_assert_mem_eq(out, expect, sizeof expect);
}
END_TEST

START_TEST(cadescbuild_scrambling_descriptor_rejects_small_cap) {
  unsigned char out[2];
  ck_assert_uint_eq(cadescbuild_scrambling_descriptor(CADESC_SCRAMBLING_MODE_CSA2, out, sizeof out), 0u);
}
END_TEST

static Suite *cadescbuild_suite(void) {
  Suite *s = suite_create("cadescbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, cadescbuild_ca_descriptor_builds_expected_bytes);
  tcase_add_test(tc, cadescbuild_ca_descriptor_rejects_small_cap);
  tcase_add_test(tc, cadescbuild_ca_descriptor_priv_appends_private_data);
  tcase_add_test(tc, cadescbuild_ca_descriptor_priv_null_matches_plain);
  tcase_add_test(tc, cadescbuild_ca_descriptor_priv_rejects_small_cap);
  tcase_add_test(tc, cadescbuild_scrambling_descriptor_builds_expected_bytes);
  tcase_add_test(tc, cadescbuild_scrambling_descriptor_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cadescbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
