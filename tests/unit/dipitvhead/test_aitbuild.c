/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/aitbuild.h"
#include "lib/demux/crc32.h"

START_TEST(aitbuild_pmt_entry_has_stream_type_and_ait_pid) {
  unsigned char out[16];
  size_t n = aitbuild_pmt_entry(0, out, sizeof out);
  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(out[0], 0x05u); /* stream_type: private sections */
  ck_assert_uint_eq((((unsigned)out[1] & 0x1F) << 8) | out[2], (unsigned)OUT_PID_AIT);
  ck_assert_uint_eq(out[5], 0x6Fu); /* application_signalling_descriptor tag */
}
END_TEST

START_TEST(aitbuild_pmt_entry_rejects_small_cap) {
  unsigned char out[4];
  ck_assert_uint_eq(aitbuild_pmt_entry(0, out, sizeof out), 0u);
}
END_TEST

START_TEST(aitbuild_ait_has_valid_header_crc_and_url) {
  unsigned char out[512];
  size_t n = aitbuild_ait(3, 0x00015A00u, 0x1234u, "http://example.invalid/app.html", out, sizeof out);

  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, n), 0u);
  ck_assert_uint_eq(out[0], 0x74u); /* AIT table_id */

  /* org_id (32 bits), right after the AIT header + application_loop_length field */
  ck_assert_uint_eq(((unsigned)out[12] << 24) | ((unsigned)out[13] << 16) | ((unsigned)out[14] << 8) | out[15], 0x00015A00u);
  ck_assert_uint_eq(((unsigned)out[16] << 8) | out[17], 0x1234u); /* app_id */
  ck_assert_uint_eq(out[18], 0x01u);                              /* application_control_code: AUTOSTART */

  ck_assert_ptr_nonnull(memmem(out, n, "http://example.invalid/app.html", strlen("http://example.invalid/app.html")));
}
END_TEST

START_TEST(aitbuild_ait_rejects_oversized_url) {
  unsigned char out[512];
  char url[300];
  memset(url, 'a', sizeof url - 1);
  url[sizeof url - 1] = '\0';
  ck_assert_uint_eq(aitbuild_ait(0, 1, 1, url, out, sizeof out), 0u);
}
END_TEST

START_TEST(aitbuild_ait_rejects_small_cap) {
  unsigned char out[16];
  ck_assert_uint_eq(aitbuild_ait(0, 1, 1, "http://x", out, sizeof out), 0u);
}
END_TEST

static Suite *aitbuild_suite(void) {
  Suite *s = suite_create("aitbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aitbuild_pmt_entry_has_stream_type_and_ait_pid);
  tcase_add_test(tc, aitbuild_pmt_entry_rejects_small_cap);
  tcase_add_test(tc, aitbuild_ait_has_valid_header_crc_and_url);
  tcase_add_test(tc, aitbuild_ait_rejects_oversized_url);
  tcase_add_test(tc, aitbuild_ait_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aitbuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
