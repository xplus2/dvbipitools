/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#include "dipisrt/bridge.h"
#include "dipisrt/version.h"

START_TEST(tssrc_cfg_file_with_path) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;
  strcpy(s.file_path, "/tmp/fixture.ts");

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_FILE);
  ck_assert_str_eq(tc.file_path, "/tmp/fixture.ts");
  ck_assert_str_eq(tc.user_agent, TOOL_NAME "/" TOOL_VERSION);
}
END_TEST

START_TEST(tssrc_cfg_file_empty_path_is_stdin) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_STDIN);
}
END_TEST

START_TEST(tssrc_cfg_http_carries_tls_and_insecure) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_HTTP;
  s.http.tls = 1;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 1, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_HTTP);
  ck_assert_int_eq(tc.http.tls, 1);
  ck_assert_int_eq(tc.insecure_tls, 1);
}
END_TEST

START_TEST(tssrc_cfg_rtp_carries_family_group_port_iface) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_RTP;
  s.family = AF_INET;
  strcpy(s.group, "239.1.1.1");
  s.port = 5000;

  plain_endpoint_to_tssrc_cfg(&s, "eth0", TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_RTP);
  ck_assert_int_eq(tc.family, AF_INET);
  ck_assert_str_eq(tc.group, "239.1.1.1");
  ck_assert_uint_eq(tc.port, 5000u);
  ck_assert_str_eq(tc.iface, "eth0");
}
END_TEST

START_TEST(tssrc_cfg_udp_kind) {
  plain_endpoint_t s;
  tssrc_cfg_t tc;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_UDP;

  plain_endpoint_to_tssrc_cfg(&s, NULL, TOOL_NAME "/" TOOL_VERSION, 0, &tc);
  ck_assert_int_eq(tc.kind, TSSRC_UDP);
}
END_TEST

START_TEST(tssink_cfg_file_with_path) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;
  strcpy(s.file_path, "/tmp/out.ts");

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_FILE);
  ck_assert_str_eq(tk.file_path, "/tmp/out.ts");
}
END_TEST

START_TEST(tssink_cfg_file_empty_path_is_stdout) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_FILE;

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_STDOUT);
}
END_TEST

START_TEST(tssink_cfg_rtp_carries_family_group_port_iface) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_RTP;
  s.family = AF_INET6;
  strcpy(s.group, "ff3e::1");
  s.port = 8700;

  plain_endpoint_to_tssink_cfg(&s, "eth1", &tk);
  ck_assert_int_eq(tk.kind, TSSINK_RTP);
  ck_assert_int_eq(tk.family, AF_INET6);
  ck_assert_str_eq(tk.group, "ff3e::1");
  ck_assert_uint_eq(tk.port, 8700u);
  ck_assert_str_eq(tk.iface, "eth1");
}
END_TEST

START_TEST(tssink_cfg_udp_kind) {
  plain_endpoint_t s;
  tssink_cfg_t tk;
  memset(&s, 0, sizeof s);
  s.kind = PLAIN_EP_UDP;

  plain_endpoint_to_tssink_cfg(&s, NULL, &tk);
  ck_assert_int_eq(tk.kind, TSSINK_UDP);
}
END_TEST

static srtout_t *open_unconnected(int queue_metrics) {
  srtout_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peers[0].host = "127.0.0.1";
  cfg.peers[0].port = 1;
  cfg.npeers = 1;
  cfg.group_mode = SRTGROUP_NONE;
  cfg.queue_metrics = queue_metrics;
  return srtout_open(&cfg);
}

START_TEST(srtout_queue_stats_count_chunks_watermark_and_drops) {
  srtout_t *r = open_unconnected(2);
  srtout_queue_stats_t st;
  unsigned char chunk[1316];
  const int written = 300;

  ck_assert_ptr_nonnull(r);
  memset(chunk, 0x47, sizeof chunk);
  srtout_queue_stats(r, &st);
  ck_assert_int_eq(st.chunks, 0);
  ck_assert_int_gt(st.capacity, 0);
  ck_assert_str_eq(st.peer_label, "127.0.0.1:1");

  for (int i = 0; i < written; i++) srtout_write(r, chunk, sizeof chunk);
  srtout_queue_stats(r, &st);
  ck_assert_int_eq(st.chunks, st.capacity);
  ck_assert_int_eq(st.high_watermark, st.capacity);
  ck_assert_uint_gt(st.dropped, 0u);
  ck_assert_uint_eq(st.dropped + (uint64_t)st.chunks, (uint64_t)written);
  srtout_close(r);
}
END_TEST

START_TEST(srtout_queue_watermark_stays_zero_below_level_two) {
  srtout_t *r = open_unconnected(1);
  srtout_queue_stats_t st;
  unsigned char chunk[1316];

  ck_assert_ptr_nonnull(r);
  memset(chunk, 0x47, sizeof chunk);
  for (int i = 0; i < 10; i++) srtout_write(r, chunk, sizeof chunk);
  srtout_queue_stats(r, &st);
  ck_assert_int_eq(st.chunks, 10);
  ck_assert_int_eq(st.high_watermark, 0);
  srtout_close(r);
}
END_TEST

static Suite *bridge_suite(void) {
  Suite *s = suite_create("dipisrt_bridge");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, tssrc_cfg_file_with_path);
  tcase_add_test(tc, tssrc_cfg_file_empty_path_is_stdin);
  tcase_add_test(tc, tssrc_cfg_http_carries_tls_and_insecure);
  tcase_add_test(tc, tssrc_cfg_rtp_carries_family_group_port_iface);
  tcase_add_test(tc, tssrc_cfg_udp_kind);
  tcase_add_test(tc, tssink_cfg_file_with_path);
  tcase_add_test(tc, tssink_cfg_file_empty_path_is_stdout);
  tcase_add_test(tc, tssink_cfg_rtp_carries_family_group_port_iface);
  tcase_add_test(tc, tssink_cfg_udp_kind);
  tcase_add_test(tc, srtout_queue_stats_count_chunks_watermark_and_drops);
  tcase_add_test(tc, srtout_queue_watermark_stays_zero_below_level_two);
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
