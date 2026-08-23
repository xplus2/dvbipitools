/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "dipisrt/bridge.h"
#include "dipisrt/version.h"

START_TEST(addr_len_ipv4) {
  ck_assert_uint_eq(addr_len(AF_INET), sizeof(struct sockaddr_in));
}
END_TEST

START_TEST(addr_len_ipv6) {
  ck_assert_uint_eq(addr_len(AF_INET6), sizeof(struct sockaddr_in6));
}
END_TEST

START_TEST(build_addr_ipv4) {
  struct sockaddr_storage ss;
  struct sockaddr_in *a = (struct sockaddr_in *)&ss;
  struct in_addr want;

  build_addr(AF_INET, "127.0.0.1", 9000, &ss);
  ck_assert_int_eq(a->sin_family, AF_INET);
  ck_assert_uint_eq(ntohs(a->sin_port), 9000u);
  inet_pton(AF_INET, "127.0.0.1", &want);
  ck_assert_int_eq(memcmp(&a->sin_addr, &want, sizeof want), 0);
}
END_TEST

START_TEST(build_addr_ipv6) {
  struct sockaddr_storage ss;
  struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
  struct in6_addr want;

  build_addr(AF_INET6, "::1", 9000, &ss);
  ck_assert_int_eq(a->sin6_family, AF_INET6);
  ck_assert_uint_eq(ntohs(a->sin6_port), 9000u);
  inet_pton(AF_INET6, "::1", &want);
  ck_assert_int_eq(memcmp(&a->sin6_addr, &want, sizeof want), 0);
}
END_TEST

START_TEST(group_mode_of_maps_all_values) {
  ck_assert_int_eq(group_mode_of(SRT_GROUP_NONE), SRTOUT_GROUP_NONE);
  ck_assert_int_eq(group_mode_of(SRT_GROUP_BROADCAST), SRTOUT_GROUP_BROADCAST);
  ck_assert_int_eq(group_mode_of(SRT_GROUP_BACKUP), SRTOUT_GROUP_BACKUP);
}
END_TEST

START_TEST(tssrc_cfg_file_with_path) {
  nonsrt_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_FILE;
  strcpy(s.file_path, "/tmp/fixture.ts");

  nonsrt_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_FILE);
  ck_assert_str_eq(tc.file_path, "/tmp/fixture.ts");
  ck_assert_str_eq(tc.user_agent, TOOL_NAME "/" TOOL_VERSION);
}
END_TEST

START_TEST(tssrc_cfg_file_empty_path_is_stdin) {
  nonsrt_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_FILE;

  nonsrt_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_STDIN);
}
END_TEST

START_TEST(tssrc_cfg_http_carries_tls_and_insecure) {
  nonsrt_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_HTTP;
  s.http.tls = 1;

  nonsrt_to_tssrc_cfg(&s, NULL, 1, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_HTTP);
  ck_assert_int_eq(tc.http.tls, 1);
  ck_assert_int_eq(tc.insecure_tls, 1);
}
END_TEST

START_TEST(tssrc_cfg_rtp_carries_family_group_port_iface) {
  nonsrt_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_RTP;
  s.family = AF_INET;
  strcpy(s.group, "239.1.1.1");
  s.port = 5000;

  nonsrt_to_tssrc_cfg(&s, "eth0", 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_RTP);
  ck_assert_int_eq(tc.family, AF_INET);
  ck_assert_str_eq(tc.group, "239.1.1.1");
  ck_assert_uint_eq(tc.port, 5000u);
  ck_assert_str_eq(tc.iface, "eth0");
}
END_TEST

START_TEST(tssrc_cfg_udp_kind) {
  nonsrt_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_UDP;

  nonsrt_to_tssrc_cfg(&s, NULL, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_UDP);
}
END_TEST

START_TEST(tssink_cfg_file_with_path) {
  nonsrt_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_FILE;
  strcpy(s.file_path, "/tmp/out.ts");

  nonsrt_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_FILE);
  ck_assert_str_eq(tk.file_path, "/tmp/out.ts");
}
END_TEST

START_TEST(tssink_cfg_file_empty_path_is_stdout) {
  nonsrt_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_FILE;

  nonsrt_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_STDOUT);
}
END_TEST

START_TEST(tssink_cfg_rtp_carries_family_group_port_iface) {
  nonsrt_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  nonsrt_to_tssink_cfg(&s, "eth1", &tk);
  ck_assert_int_eq(tk.kind, TSSINK_RTP);
  ck_assert_int_eq(tk.family, AF_INET6);
  ck_assert_str_eq(tk.group, "ff3e::1");
  ck_assert_uint_eq(tk.port, 8700u);
  ck_assert_str_eq(tk.iface, "eth1");
}
END_TEST

START_TEST(tssink_cfg_udp_kind) {
  nonsrt_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = NONSRT_UDP;

  nonsrt_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_UDP);
}
END_TEST

static Suite *bridge_suite(void) {
  Suite *s = suite_create("dipisrt_bridge");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, addr_len_ipv4);
  tcase_add_test(tc, addr_len_ipv6);
  tcase_add_test(tc, build_addr_ipv4);
  tcase_add_test(tc, build_addr_ipv6);
  tcase_add_test(tc, group_mode_of_maps_all_values);
  tcase_add_test(tc, tssrc_cfg_file_with_path);
  tcase_add_test(tc, tssrc_cfg_file_empty_path_is_stdin);
  tcase_add_test(tc, tssrc_cfg_http_carries_tls_and_insecure);
  tcase_add_test(tc, tssrc_cfg_rtp_carries_family_group_port_iface);
  tcase_add_test(tc, tssrc_cfg_udp_kind);
  tcase_add_test(tc, tssink_cfg_file_with_path);
  tcase_add_test(tc, tssink_cfg_file_empty_path_is_stdout);
  tcase_add_test(tc, tssink_cfg_rtp_carries_family_group_port_iface);
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
