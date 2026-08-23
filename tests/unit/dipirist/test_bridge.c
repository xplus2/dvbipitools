/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipirist/bridge.h"
#include "dipirist/version.h"

START_TEST(profile_of_maps_simple) {
  ck_assert_int_eq(profile_of(RIST_PROF_SIMPLE), RIST_PROFILE_SIMPLE);
}
END_TEST

START_TEST(profile_of_maps_main) {
  ck_assert_int_eq(profile_of(RIST_PROF_MAIN), RIST_PROFILE_MAIN);
}
END_TEST

START_TEST(tssrc_cfg_file_with_path) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_FILE;
  strcpy(s.file_path, "/tmp/fixture.ts");

  nonrist_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_FILE);
  ck_assert_str_eq(tc.file_path, "/tmp/fixture.ts");
  ck_assert_str_eq(tc.user_agent, TOOL_NAME "/" TOOL_VERSION);
}
END_TEST

START_TEST(tssrc_cfg_file_empty_path_is_stdin) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_FILE;

  nonrist_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_STDIN);
}
END_TEST

START_TEST(tssrc_cfg_http_carries_tls_and_insecure) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_HTTP;
  s.http.tls = 1;

  nonrist_to_tssrc_cfg(&s, NULL, 1, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_HTTP);
  ck_assert_int_eq(tc.http.tls, 1);
  ck_assert_int_eq(tc.insecure_tls, 1);
}
END_TEST

START_TEST(tssrc_cfg_rtp_ipv4_carries_family_group_port_iface) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_RTP;
  s.family = AF_INET;
  strcpy(s.group, "239.1.1.1");
  s.port = 5000;

  nonrist_to_tssrc_cfg(&s, "eth0", 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_RTP);
  ck_assert_int_eq(tc.family, AF_INET);
  ck_assert_str_eq(tc.group, "239.1.1.1");
  ck_assert_uint_eq(tc.port, 5000u);
  ck_assert_str_eq(tc.iface, "eth0");
}
END_TEST

START_TEST(tssrc_cfg_rtp_ipv6_carries_family) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  nonrist_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.family, AF_INET6);
  ck_assert_str_eq(tc.group, "ff3e::1");
}
END_TEST

START_TEST(tssrc_cfg_udp_kind) {
  nonrist_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_UDP;

  nonrist_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_UDP);
}
END_TEST

START_TEST(tssink_cfg_file_with_path) {
  nonrist_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_FILE;
  strcpy(s.file_path, "/tmp/out.ts");

  nonrist_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_FILE);
  ck_assert_str_eq(tk.file_path, "/tmp/out.ts");
}
END_TEST

START_TEST(tssink_cfg_file_empty_path_is_stdout) {
  nonrist_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_FILE;

  nonrist_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_STDOUT);
}
END_TEST

START_TEST(tssink_cfg_rtp_ipv6_carries_family_group_port_iface) {
  nonrist_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  nonrist_to_tssink_cfg(&s, "eth1", &tk);
  ck_assert_int_eq(tk.kind, TSSINK_RTP);
  ck_assert_int_eq(tk.family, AF_INET6);
  ck_assert_str_eq(tk.group, "ff3e::1");
  ck_assert_uint_eq(tk.port, 8700u);
  ck_assert_str_eq(tk.iface, "eth1");
}
END_TEST

START_TEST(tssink_cfg_udp_kind) {
  nonrist_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONRIST_UDP;

  nonrist_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_UDP);
}
END_TEST

static Suite *bridge_suite(void) {
  Suite *s = suite_create("dipirist_bridge");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, profile_of_maps_simple);
  tcase_add_test(tc, profile_of_maps_main);
  tcase_add_test(tc, tssrc_cfg_file_with_path);
  tcase_add_test(tc, tssrc_cfg_file_empty_path_is_stdin);
  tcase_add_test(tc, tssrc_cfg_http_carries_tls_and_insecure);
  tcase_add_test(tc, tssrc_cfg_rtp_ipv4_carries_family_group_port_iface);
  tcase_add_test(tc, tssrc_cfg_rtp_ipv6_carries_family);
  tcase_add_test(tc, tssrc_cfg_udp_kind);
  tcase_add_test(tc, tssink_cfg_file_with_path);
  tcase_add_test(tc, tssink_cfg_file_empty_path_is_stdout);
  tcase_add_test(tc, tssink_cfg_rtp_ipv6_carries_family_group_port_iface);
  tcase_add_test(tc, tssink_cfg_udp_kind);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bridge_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
