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
  size_t n = aitbuild_pmt_entry(0x11F, out, sizeof out);
  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(out[0], 0x05u); /* stream_type: private sections */
  ck_assert_uint_eq((((unsigned)out[1] & 0x1F) << 8) | out[2], 0x11Fu);
  ck_assert_uint_eq(out[5], 0x6Fu); /* application_signalling_descriptor tag */
}
END_TEST

START_TEST(aitbuild_pmt_entry_rejects_small_cap) {
  unsigned char out[4];
  ck_assert_uint_eq(aitbuild_pmt_entry(0x11F, out, sizeof out), 0u);
}
END_TEST

static const unsigned char *find_desc(const unsigned char *sec, unsigned tag) {
  size_t pos = 21;
  size_t end = 21 + ((((size_t)sec[19] & 0x0F) << 8) | sec[20]);
  while (pos + 2 <= end) {
    if (sec[pos] == tag) return sec + pos;
    pos += 2 + sec[pos + 1];
  }
  return NULL;
}

START_TEST(aitbuild_ait_has_valid_header_crc_and_url) {
  unsigned char out[512];
  size_t n = aitbuild_ait(3, 0x00015A00u, 0x1234u, "http://example.invalid/app.html", NULL, out, sizeof out);

  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, n), 0u);
  ck_assert_uint_eq(out[0], 0x74u); /* AIT table_id */

  /* org_id (32 bits), right after the AIT header + application_loop_length field */
  ck_assert_uint_eq(((unsigned)out[12] << 24) | ((unsigned)out[13] << 16) | ((unsigned)out[14] << 8) | out[15], 0x00015A00u);
  ck_assert_uint_eq(((unsigned)out[16] << 8) | out[17], 0x1234u); /* app_id */
  ck_assert_uint_eq(out[18], 0x01u);                              /* application_control_code: AUTOSTART */

  const unsigned char *tp = find_desc(out, 0x02);
  ck_assert_ptr_nonnull(tp);
  ck_assert_uint_eq(((unsigned)tp[2] << 8) | tp[3], 0x0003u);
  ck_assert_uint_eq(tp[5], strlen("http://example.invalid/")); /* URL_base_length */
  ck_assert_int_eq(memcmp(tp + 6, "http://example.invalid/", 23), 0);
  ck_assert_uint_eq(tp[6 + 23], 0u); /* URL_extension_count */
  ck_assert_uint_eq(tp[1], 2 + 1 + 1 + 23 + 1);

  const unsigned char *nm = find_desc(out, 0x01);
  ck_assert_ptr_nonnull(nm);
  ck_assert_uint_eq(nm[1], 3u + 1u + nm[5]);
  ck_assert_uint_eq(nm[5], 5u);
  ck_assert_int_eq(memcmp(nm + 6, "HbbTV", 5), 0);

  const unsigned char *loc = find_desc(out, 0x15);
  ck_assert_ptr_nonnull(loc);
  ck_assert_uint_eq(loc[1], strlen("app.html"));
  ck_assert_int_eq(memcmp(loc + 2, "app.html", 8), 0);
}
END_TEST

START_TEST(aitbuild_ait_host_only_url_has_empty_initial_path) {
  unsigned char out[512];
  size_t n = aitbuild_ait(0, 1, 1, "http://example.invalid", NULL, out, sizeof out);
  ck_assert_uint_ne(n, 0u);
  const unsigned char *tp = find_desc(out, 0x02);
  ck_assert_ptr_nonnull(tp);
  ck_assert_uint_eq(tp[5], strlen("http://example.invalid"));
  const unsigned char *loc = find_desc(out, 0x15);
  ck_assert_ptr_nonnull(loc);
  ck_assert_uint_eq(loc[1], 0u);
}
END_TEST

START_TEST(aitbuild_ait_uses_custom_name) {
  unsigned char out[512];
  size_t n = aitbuild_ait(0, 1, 1, "http://example.invalid/a.html", "My App", out, sizeof out);
  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, n), 0u);
  const unsigned char *nm = find_desc(out, 0x01);
  ck_assert_ptr_nonnull(nm);
  ck_assert_uint_eq(nm[1], 10u);
  ck_assert_uint_eq(nm[5], 6u);
  ck_assert_int_eq(memcmp(nm + 6, "My App", 6), 0);
}
END_TEST

START_TEST(aitbuild_ait_rejects_oversized_name) {
  unsigned char out[512];
  char name[65];
  memset(name, 'n', sizeof name - 1);
  name[sizeof name - 1] = '\0';
  ck_assert_uint_eq(aitbuild_ait(0, 1, 1, "http://x/", name, out, sizeof out), 0u);
}
END_TEST

START_TEST(aitbuild_ait_max_url_and_name_fit_section) {
  unsigned char out[320];
  char url[201];
  char name[64];
  memset(url, 'a', sizeof url - 1);
  memcpy(url, "http://", 7);
  url[sizeof url - 1] = '\0';
  memset(name, 'n', sizeof name - 1);
  name[sizeof name - 1] = '\0';
  size_t n = aitbuild_ait(0, 1, 1, url, name, out, sizeof out);
  ck_assert_uint_ne(n, 0u);
  ck_assert_uint_eq(crc32_mpeg(out, n), 0u);
}
END_TEST

START_TEST(aitbuild_ait_rejects_oversized_url) {
  unsigned char out[512];
  char url[300];
  memset(url, 'a', sizeof url - 1);
  url[sizeof url - 1] = '\0';
  ck_assert_uint_eq(aitbuild_ait(0, 1, 1, url, NULL, out, sizeof out), 0u);
}
END_TEST

START_TEST(aitbuild_ait_rejects_small_cap) {
  unsigned char out[16];
  ck_assert_uint_eq(aitbuild_ait(0, 1, 1, "http://x", NULL, out, sizeof out), 0u);
}
END_TEST

static Suite *aitbuild_suite(void) {
  Suite *s = suite_create("aitbuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aitbuild_pmt_entry_has_stream_type_and_ait_pid);
  tcase_add_test(tc, aitbuild_pmt_entry_rejects_small_cap);
  tcase_add_test(tc, aitbuild_ait_has_valid_header_crc_and_url);
  tcase_add_test(tc, aitbuild_ait_host_only_url_has_empty_initial_path);
  tcase_add_test(tc, aitbuild_ait_uses_custom_name);
  tcase_add_test(tc, aitbuild_ait_rejects_oversized_name);
  tcase_add_test(tc, aitbuild_ait_max_url_and_name_fit_section);
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
