/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "dipisrt/bridge.h"
#include "lib/mux/fec2022.h"
#include "lib/sys/signal.h"
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

START_TEST(dedup_is_duplicate_false_on_empty_history) {
  dedup_entry_t hist[6] = {{0, 0}};
  ck_assert_int_eq(dedup_is_duplicate(hist, 6, 0x12345678u, 100), 0);
}
END_TEST

START_TEST(dedup_record_then_is_duplicate_matches_same_hash_and_len) {
  dedup_entry_t hist[6] = {{0, 0}};
  int next = 0;
  dedup_record(hist, 6, &next, 0xAABBCCDDu, 188);
  ck_assert_int_eq(next, 1);
  ck_assert_int_eq(dedup_is_duplicate(hist, 6, 0xAABBCCDDu, 188), 1);
}
END_TEST

START_TEST(dedup_is_duplicate_requires_len_match_too) {
  dedup_entry_t hist[6] = {{0, 0}};
  int next = 0;
  dedup_record(hist, 6, &next, 0xAABBCCDDu, 188);
  ck_assert_int_eq(dedup_is_duplicate(hist, 6, 0xAABBCCDDu, 189), 0);
}
END_TEST

START_TEST(dedup_record_wraps_ring_and_overwrites_oldest) {
  dedup_entry_t hist[3] = {{0, 0}};
  int next = 0;
  dedup_record(hist, 3, &next, 1, 10);
  dedup_record(hist, 3, &next, 2, 10);
  dedup_record(hist, 3, &next, 3, 10);
  ck_assert_int_eq(next, 0);
  ck_assert_int_eq(dedup_is_duplicate(hist, 3, 1, 10), 1);
  dedup_record(hist, 3, &next, 4, 10);
  ck_assert_int_eq(next, 1);
  ck_assert_int_eq(dedup_is_duplicate(hist, 3, 1, 10), 0);
  ck_assert_int_eq(dedup_is_duplicate(hist, 3, 4, 10), 1);
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

typedef enum {
  SB_SEND_SOURCE_FAILS,
  SB_SEND_PEER_COUNT,
  SB_RECV_SINK_FAILS,
  SB_RECV_PEER_COUNT,
  SB_RECV_BAD_PASSPHRASE
} srt_bridge_fail_t;

typedef struct {
  srt_bridge_fail_t kind;
  const char *msg;
} srt_bridge_fail_case_t;

static const srt_bridge_fail_case_t srt_bridge_fail_cases[] = {
  {SB_SEND_SOURCE_FAILS, NULL},
  {SB_SEND_PEER_COUNT, "plain connection needs exactly one peer"},
  {SB_RECV_SINK_FAILS, NULL},
  {SB_RECV_PEER_COUNT, "plain connection needs exactly one peer"},
  {SB_RECV_BAD_PASSPHRASE, NULL},
};

static void srt_file_endpoint(endpoint_t *e, const char *path) {
  memset(e, 0, sizeof *e);
  e->nonsrt.kind = PLAIN_EP_FILE;
  snprintf(e->nonsrt.file_path, sizeof e->nonsrt.file_path, "%s", path);
}

static void srt_peer_endpoint(endpoint_t *e, int peers, int listen) {
  memset(e, 0, sizeof *e);
  e->is_srt = 1;
  e->listen = listen;
  e->n_srt = peers;
  for (int i = 0; i < peers; i++) {
    snprintf(e->srt_host[i], sizeof e->srt_host[i], "127.0.0.1");
    e->srt_port[i] = (unsigned)(9 + i);
    e->family[i] = AF_INET;
  }
}

START_TEST(bridge_run_fails_cleanly_when_setup_cannot_complete) {
  const srt_bridge_fail_case_t *c = &srt_bridge_fail_cases[_i];
  config_t cfg;
  metrics_exporter_t mx;
  char msg[4096];
  int rc;

  memset(&cfg, 0, sizeof cfg);
  memset(&mx, 0, sizeof mx);
  switch (c->kind) {
    case SB_SEND_SOURCE_FAILS:
      srt_file_endpoint(&cfg.in, "/nonexistent-dir/in.ts");
      srt_peer_endpoint(&cfg.out, 1, 0);
      break;
    case SB_SEND_PEER_COUNT:
      srt_file_endpoint(&cfg.in, "/dev/null");
      srt_peer_endpoint(&cfg.out, 2, 0);
      break;
    case SB_RECV_SINK_FAILS:
      srt_peer_endpoint(&cfg.in, 1, 1);
      srt_file_endpoint(&cfg.out, "/nonexistent-dir/out.ts");
      break;
    case SB_RECV_PEER_COUNT:
      srt_peer_endpoint(&cfg.in, 2, 1);
      srt_file_endpoint(&cfg.out, "/dev/null");
      break;
    case SB_RECV_BAD_PASSPHRASE:
      srt_peer_endpoint(&cfg.in, 1, 1);
      srt_file_endpoint(&cfg.out, "/dev/null");
      snprintf(cfg.passphrase, sizeof cfg.passphrase, "short");
      break;
  }
  cfg.n_in = 1;
  cfg.n_out = 1;
  log_capture_begin();
  rc = bridge_run(&cfg, &mx);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, 1);
  if (c->msg) ck_assert_msg(strstr(msg, c->msg) != NULL, "log=[%s]", msg);
}
END_TEST

static unsigned bridge_free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static void bridge_rtp_ts(unsigned char *out, uint16_t seq, unsigned char id) {
  memset(out, 0xFF, 12 + 188);
  out[0] = 0x80;
  out[1] = 33;
  out[2] = (unsigned char)(seq >> 8);
  out[3] = (unsigned char)seq;
  out[11] = 9;
  out[12] = 0x47;
  out[13] = id;
}

START_TEST(sender_recovers_lost_packet_from_repair_without_further_source) {
  enum { PKTS = 4, RTP_LEN = 12 + 188 };
  fec2022_enc_t *enc = fec2022_enc_new(2, 2, 96);
  unsigned char pkt[PKTS][RTP_LEN];
  unsigned char repair[FEC2022_MAX_REPAIR];
  unsigned char rx[PKTS * 188];
  struct sockaddr_in src_dst;
  struct sockaddr_in fec_dst;
  struct timespec ts = {0, 500000000L};
  srtin_cfg_t ic;
  srtin_t *in;
  config_t cfg;
  metrics_exporter_t mx;
  char group[32];
  unsigned port = bridge_free_udp_port();
  unsigned fec_port = bridge_free_udp_port();
  unsigned srt_port = bridge_free_udp_port();
  size_t rlen = 0;
  size_t got = 0;
  time_t deadline;
  int src_sock;
  int fec_sock;
  int status = 0;
  pid_t pid;

  ck_assert_ptr_nonnull(enc);
  snprintf(group, sizeof group, "239.78.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
  memset(&cfg, 0, sizeof cfg);
  memset(&mx, 0, sizeof mx);
  cfg.in.nonsrt.kind = PLAIN_EP_RTP;
  cfg.in.nonsrt.family = AF_INET;
  snprintf(cfg.in.nonsrt.group, sizeof cfg.in.nonsrt.group, "%s", group);
  cfg.in.nonsrt.port = port;
  cfg.in.nonsrt.al_fec_l = 2;
  cfg.in.nonsrt.al_fec_d = 2;
  cfg.in.nonsrt.al_fec_port = fec_port;
  srt_peer_endpoint(&cfg.out, 1, 0);
  cfg.out.srt_port[0] = srt_port;
  cfg.n_in = 1;
  cfg.n_out = 1;

  memset(&ic, 0, sizeof ic);
  ic.peers[0].host = "127.0.0.1";
  ic.peers[0].port = srt_port;
  ic.npeers = 1;
  ic.listen = 1;

  pid = fork();
  ck_assert_int_ge(pid, 0);
  if (pid == 0) {
    signals_install();
    nanosleep(&ts, NULL);
    _exit(bridge_run(&cfg, &mx));
  }

  in = srtin_open(&ic);
  ck_assert_ptr_nonnull(in);

  src_sock = socket(AF_INET, SOCK_DGRAM, 0);
  fec_sock = socket(AF_INET, SOCK_DGRAM, 0);
  ck_assert_int_ge(src_sock, 0);
  ck_assert_int_ge(fec_sock, 0);
  memset(&src_dst, 0, sizeof src_dst);
  src_dst.sin_family = AF_INET;
  src_dst.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &src_dst.sin_addr), 1);
  fec_dst = src_dst;
  fec_dst.sin_port = htons((unsigned short)fec_port);

  for (unsigned i = 0; i < PKTS; i++) {
    bridge_rtp_ts(pkt[i], (uint16_t)(0x3000 + i), (unsigned char)i);
    rlen = fec2022_enc_feed(enc, pkt[i], RTP_LEN, 1000 + i, repair, sizeof repair);
  }
  ck_assert_uint_gt(rlen, 0u);
  for (unsigned i = 0; i < PKTS - 1; i++)
    ck_assert_int_eq((int)sendto(src_sock, pkt[i], RTP_LEN, 0, (struct sockaddr *)&src_dst, sizeof src_dst), RTP_LEN);
  ck_assert_int_eq((int)sendto(fec_sock, repair, rlen, 0, (struct sockaddr *)&fec_dst, sizeof fec_dst), (int)rlen);

  deadline = time(NULL) + 5;
  while (got < sizeof rx && time(NULL) < deadline) {
    unsigned char buf[2048];
    int reconnected = 0;
    int n = srtin_read(in, buf, sizeof buf, &reconnected);

    ck_assert_int_ge(n, 0);
    if (n > 0 && got + (size_t)n <= sizeof rx) {
      memcpy(rx + got, buf, (size_t)n);
      got += (size_t)n;
    }
  }
  kill(pid, SIGTERM);
  waitpid(pid, &status, 0);
  srtin_close(in);
  close(src_sock);
  close(fec_sock);
  fec2022_enc_free(enc);

  ck_assert_uint_eq(got, sizeof rx);
  for (unsigned i = 0; i < PKTS; i++) {
    ck_assert_uint_eq(rx[i * 188], 0x47);
    ck_assert_uint_eq(rx[i * 188 + 1], i);
  }
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
  tcase_add_loop_test(tc, bridge_run_fails_cleanly_when_setup_cannot_complete, 0, (int)(sizeof srt_bridge_fail_cases / sizeof srt_bridge_fail_cases[0]));
  tcase_add_test(tc, sender_recovers_lost_packet_from_repair_without_further_source);
  tcase_add_test(tc, dedup_is_duplicate_false_on_empty_history);
  tcase_add_test(tc, dedup_record_then_is_duplicate_matches_same_hash_and_len);
  tcase_add_test(tc, dedup_is_duplicate_requires_len_match_too);
  tcase_add_test(tc, dedup_record_wraps_ring_and_overwrites_oldest);
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
