/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/demux/rtcp.h"
#include "lib/helper/signal.h"
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

static mcast_t *open_scratch_main(unsigned port) {
  return mcast_open(AF_INET, "239.7.9.72", port, NULL, 5);
}

static rtp_hdr_t make_hdr(uint32_t ssrc, uint16_t seq) {
  rtp_hdr_t h;
  memset(&h, 0, sizeof h);
  h.ssrc = ssrc;
  h.seq = seq;
  return h;
}

static int g_rams_r_calls;
static rtcp_rams_r_t g_rams_r;

static void rams_r_cb(const rtcp_rams_r_t *req, void *user) {
  (void)user;
  g_rams_r_calls++;
  g_rams_r = *req;
}

START_TEST(open_sends_rams_r_with_ignore_media_ssrc_on_the_wire) {
  int listener = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  fcc_client_t *c;
  unsigned char rbuf[256];
  ssize_t n;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(16201);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(listener, 0);
  ck_assert_int_eq(bind(listener, (struct sockaddr *)&addr, sizeof addr), 0);

  c = open_client(16201);
  ck_assert_ptr_nonnull(c);

  n = recv(listener, rbuf, sizeof rbuf, 0);
  ck_assert_int_gt(n, 0);

  g_rams_r_calls = 0;
  rtcp_parse(rbuf, (size_t)n, NULL, rams_r_cb, NULL, NULL, NULL, NULL, NULL);
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 1);

  fcc_client_close(c);
  close(listener);
}
END_TEST

START_TEST(burst_packet_is_delivered_before_cutover) {
  fcc_client_t *c = open_client(16202);
  mcast_t *main_ = open_scratch_main(16302);
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
  int listener = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  fcc_client_t *c;
  mcast_t *main_ = open_scratch_main(16303);
  unsigned char buf[64], rbuf[256];
  rtp_hdr_t h;
  ssize_t n;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(16203);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  ck_assert_int_ge(listener, 0);
  ck_assert_int_eq(bind(listener, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_ptr_nonnull(main_);

  c = open_client(16203);
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
  rtcp_parse(rbuf, (size_t)n, NULL, NULL, NULL, rams_t_cb, NULL, NULL, NULL);
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
  fcc_client_t *c = open_client(16204);
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
  fcc_client_t *c = open_client(16205);
  mcast_t *main_ = open_scratch_main(16305);
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

  ck_assert_int_eq(fcc_client_read(c, main_, buf, sizeof buf), 0); /* stray burst packet dropped */

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
