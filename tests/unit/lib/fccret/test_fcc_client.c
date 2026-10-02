/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/demux/rtcp.h"
#include "lib/sys/signal.h"
#include "lib/mux/rtcp_build.h"
#include "lib/mux/rtx.h"
#include "lib/fccret/fcc_client.h"

static fcc_client_t *open_client(unsigned server_port) {
  fcc_client_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "127.0.0.1");
  cfg.port = server_port;
  cfg.rtx_pt = 99;
  return fcc_client_open(&cfg);
}

static mcast_t *open_scratch_main(void) {
  return mcast_open(AF_INET, "239.7.9.72", 0, NULL, 5);
}

static int bind_listener(unsigned *port_out) {
  struct sockaddr_in addr;
  socklen_t len = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &len), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static rtp_hdr_t make_hdr(uint32_t ssrc, uint16_t seq) {
  rtp_hdr_t h;
  memset(&h, 0, sizeof h);
  h.ssrc = ssrc;
  h.seq = seq;
  return h;
}

static void on_alarm(int sig) {
  (void)sig;
}

static int read_quiet(fcc_client_t *c, mcast_t *main_, unsigned char *buf, size_t cap) {
  struct sigaction sa;
  struct itimerval it;

  memset(&sa, 0, sizeof sa);
  sa.sa_handler = on_alarm;
  sigemptyset(&sa.sa_mask);
  ck_assert_int_eq(sigaction(SIGALRM, &sa, NULL), 0);
  memset(&it, 0, sizeof it);
  it.it_value.tv_usec = 20000;
  ck_assert_int_eq(setitimer(ITIMER_REAL, &it, NULL), 0);
  return (int)fcc_client_read(c, main_, buf, cap);
}

static int g_rams_r_calls;
static rtcp_rams_r_t g_rams_r;

static void rams_r_cb(const rtcp_rams_r_t *req, void *user) {
  (void)user;
  g_rams_r_calls++;
  g_rams_r = *req;
}

START_TEST(open_sends_rams_r_with_ignore_media_ssrc_on_the_wire) {
  unsigned port;
  int listener = bind_listener(&port);
  fcc_client_t *c;
  unsigned char rbuf[256];
  ssize_t n;

  c = open_client(port);
  ck_assert_ptr_nonnull(c);

  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);

  g_rams_r_calls = 0;
  rtcp_parse(rbuf, (size_t)n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 1);

  fcc_client_close(c);
  close(listener);
}
END_TEST

START_TEST(burst_packet_is_delivered_before_cutover) {
  fcc_client_t *c = open_client(9);
  mcast_t *main_ = open_scratch_main();
  unsigned char rtx[128];
  unsigned char buf[64];
  _Atomic uint16_t rtx_seq = 0;
  size_t rtxlen;
  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(main_);

  rtxlen = rtx_build(&rtx_seq, 0xAAAA, 99, 90000, 5, (const unsigned char *)"burst", 5, rtx, sizeof rtx);
  ck_assert_uint_gt(rtxlen, 0u);
  fcc_on_uni(c, rtx, rtxlen, mono_seconds());

  ck_assert_int_eq(fcc_client_done(c), 0);
  ck_assert_int_eq(fcc_client_read(c, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "burst", 5);

  fcc_client_close(c);
  mcast_close(main_);
}
END_TEST

static int g_rams_t_calls;
static rtcp_rams_t_t g_rams_t;

static void rams_t_cb(const rtcp_rams_t_t *term, void *user) {
  (void)user;
  g_rams_t_calls++;
  g_rams_t = *term;
}

START_TEST(first_multicast_packet_triggers_rams_t_and_cutover) {
  unsigned port;
  int listener = bind_listener(&port);
  fcc_client_t *c;
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[64], rbuf[256];
  rtp_hdr_t h;
  ssize_t n;

  ck_assert_ptr_nonnull(main_);

  c = open_client(port);
  ck_assert_ptr_nonnull(c);
  recv(listener, rbuf, sizeof rbuf, 0); /* drain the initial RAMS-R */

  h = make_hdr(0xBBBB, 42);
  fcc_on_multicast(c, &h, (const unsigned char *)"live!", 5, mono_seconds());

  ck_assert_int_eq(fcc_client_done(c), 1);
  ck_assert_int_eq(fcc_client_read(c, main_, buf, sizeof buf), 5);
  ck_assert_mem_eq(buf, "live!", 5);

  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);
  g_rams_t_calls = 0;
  rtcp_parse(rbuf, (size_t)n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb});
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 1);
  ck_assert_uint_eq(g_rams_t.first_mc_seqnum, 42u);
  ck_assert_uint_eq(g_rams_t.media_ssrc, 0xBBBBu);

  fcc_client_close(c);
  mcast_close(main_);
  close(listener);
}
END_TEST

START_TEST(rejected_rams_i_response_sets_done_without_multicast) {
  fcc_client_t *c = open_client(9);
  unsigned char pkt[32];
  size_t n;
  ck_assert_ptr_nonnull(c);

  n = rtcp_build_rams_i(0, 0, 0, 510, NULL, pkt, sizeof pkt);
  ck_assert_uint_gt(n, 0u);
  fcc_on_uni(c, pkt, n, mono_seconds());

  ck_assert_int_eq(fcc_client_done(c), 1);

  fcc_client_close(c);
}
END_TEST

START_TEST(burst_packet_after_done_is_ignored) {
  fcc_client_t *c = open_client(9);
  mcast_t *main_ = open_scratch_main();
  unsigned char rtx[128], buf[64];
  _Atomic uint16_t rtx_seq = 0;
  rtp_hdr_t h;
  size_t rtxlen;
  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(main_);

  h = make_hdr(0xCCCC, 7);
  fcc_on_multicast(c, &h, (const unsigned char *)"live", 4, mono_seconds());
  ck_assert_int_eq(fcc_client_read(c, main_, buf, sizeof buf), 4); /* consume the cutover payload */

  rtxlen = rtx_build(&rtx_seq, 0xCCCC, 99, 90000, 8, (const unsigned char *)"late", 4, rtx, sizeof rtx);
  fcc_on_uni(c, rtx, rtxlen, mono_seconds());

  ck_assert_int_eq(read_quiet(c, main_, buf, sizeof buf), 0);

  fcc_client_close(c);
  mcast_close(main_);
}
END_TEST


START_TEST(rams_r_carries_configured_buffer_fill_bounds) {
  unsigned port;
  int listener = bind_listener(&port);
  fcc_client_cfg_t cfg;
  fcc_client_t *c;
  unsigned char rbuf[256];
  ssize_t n;

  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "127.0.0.1");
  cfg.port = port;
  cfg.rtx_pt = 99;
  cfg.min_buffer_fill_ms = 500;
  cfg.max_buffer_fill_ms = 5000;
  c = fcc_client_open(&cfg);
  ck_assert_ptr_nonnull(c);
  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);
  g_rams_r_calls = 0;
  rtcp_parse(rbuf, (size_t)n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.has_min_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.min_buffer_fill_ms, 500u);
  ck_assert_int_eq(g_rams_r.has_max_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.max_buffer_fill_ms, 5000u);
  fcc_client_close(c);
  close(listener);
}
END_TEST

START_TEST(rams_r_without_bounds_omits_buffer_fill_tlvs) {
  unsigned port;
  int listener = bind_listener(&port);
  fcc_client_t *c = open_client(port);
  unsigned char rbuf[256];
  ssize_t n;

  ck_assert_ptr_nonnull(c);
  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);
  g_rams_r_calls = 0;
  rtcp_parse(rbuf, (size_t)n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.has_min_buffer_fill, 0);
  ck_assert_int_eq(g_rams_r.has_max_buffer_fill, 0);
  fcc_client_close(c);
  close(listener);
}
END_TEST

START_TEST(open_fails_on_bad_address_unsupported_family_and_connect_error) {
  fcc_client_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.addr, "not-an-address");
  cfg.port = 9;
  ck_assert_ptr_null(fcc_client_open(&cfg));
  strcpy(cfg.addr, "255.255.255.255");
  ck_assert_ptr_null(fcc_client_open(&cfg));
  strcpy(cfg.addr, "127.0.0.1");
  cfg.family = -1;
  ck_assert_ptr_null(fcc_client_open(&cfg));
}
END_TEST

START_TEST(unicast_burst_arrives_through_read) {
  unsigned port;
  int server = bind_listener(&port);
  fcc_client_t *c = open_client(port);
  mcast_t *main_ = open_scratch_main();
  struct sockaddr_in client;
  socklen_t clen = sizeof client;
  unsigned char rbuf[256];
  unsigned char rtx[128];
  unsigned char buf[64];
  _Atomic uint16_t rtx_seq = 0;
  size_t rtxlen;
  int i;

  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(main_);
  ck_assert_int_gt(recvfrom(server, rbuf, sizeof rbuf, 0, (struct sockaddr *)&client, &clen), 0);
  rtxlen = rtx_build(&rtx_seq, 0xAAAA, 99, 90000, 5, (const unsigned char *)"wire!", 5, rtx, sizeof rtx);
  ck_assert_int_eq(sendto(server, rtx, rtxlen, 0, (struct sockaddr *)&client, clen), (int)rtxlen);
  for (i = 0; i < 20; i++) {
    ssize_t n = fcc_client_read(c, main_, buf, sizeof buf);
    if (n > 0) {
      ck_assert_int_eq(n, 5);
      ck_assert_mem_eq(buf, "wire!", 5);
      break;
    }
  }
  ck_assert_int_lt(i, 20);
  fcc_client_close(c);
  mcast_close(main_);
  close(server);
}
END_TEST

START_TEST(read_interrupted_by_signal_returns_zero) {
  fcc_client_t *c = open_client(9);
  mcast_t *main_ = open_scratch_main();
  unsigned char buf[16];

  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(main_);
  ck_assert_int_eq(read_quiet(c, main_, buf, sizeof buf), 0);
  fcc_client_close(c);
  mcast_close(main_);
}
END_TEST

START_TEST(accepted_rams_i_responses_keep_bursting) {
  fcc_client_t *c = open_client(9);
  unsigned char pkt[32];
  size_t n;

  ck_assert_ptr_nonnull(c);
  n = rtcp_build_rams_i(0, 0, 0, 200, NULL, pkt, sizeof pkt);
  fcc_on_uni(c, pkt, n, mono_seconds());
  ck_assert_int_eq(fcc_client_done(c), 0);
  n = rtcp_build_rams_i(0, 0, 0, 100, NULL, pkt, sizeof pkt);
  fcc_on_uni(c, pkt, n, mono_seconds());
  ck_assert_int_eq(fcc_client_done(c), 0);
  fcc_on_uni(c, pkt, 1, mono_seconds());
  fcc_on_uni(c, (const unsigned char *)"\x80\x63xxxx", 6, mono_seconds());
  ck_assert_int_eq(fcc_client_done(c), 0);
  fcc_client_close(c);
}
END_TEST

START_TEST(read_truncates_to_caller_capacity_and_clips_oversized_cutover) {
  fcc_client_t *c = open_client(9);
  mcast_t *main_ = open_scratch_main();
  static unsigned char big[70000];
  static unsigned char out[70000];
  rtp_hdr_t h = make_hdr(0xDDDD, 1);

  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(main_);
  memset(big, 0x42, sizeof big);
  fcc_on_multicast(c, &h, big, sizeof big, mono_seconds());
  ck_assert_int_eq(fcc_client_read(c, main_, out, 10), 10);
  fcc_on_multicast(c, &h, big, 5, mono_seconds());
  ck_assert_int_eq(read_quiet(c, main_, out, sizeof out), 0);
  fcc_client_close(c);
  mcast_close(main_);
}
END_TEST

static Suite *fcc_client_suite(void) {
  Suite *s = suite_create("lib_fccret_fcc_client");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, open_sends_rams_r_with_ignore_media_ssrc_on_the_wire);
  tcase_add_test(tc, burst_packet_is_delivered_before_cutover);
  tcase_add_test(tc, first_multicast_packet_triggers_rams_t_and_cutover);
  tcase_add_test(tc, rejected_rams_i_response_sets_done_without_multicast);
  tcase_add_test(tc, burst_packet_after_done_is_ignored);
  tcase_add_test(tc, rams_r_carries_configured_buffer_fill_bounds);
  tcase_add_test(tc, rams_r_without_bounds_omits_buffer_fill_tlvs);
  tcase_add_test(tc, open_fails_on_bad_address_unsupported_family_and_connect_error);
  tcase_add_test(tc, unicast_burst_arrives_through_read);
  tcase_add_test(tc, read_interrupted_by_signal_returns_zero);
  tcase_add_test(tc, accepted_rams_i_responses_keep_bursting);
  tcase_add_test(tc, read_truncates_to_caller_capacity_and_clips_oversized_cutover);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fcc_client_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
