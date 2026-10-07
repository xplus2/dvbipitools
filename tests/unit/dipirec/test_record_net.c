/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipirec/config.h"
#include "dipirec/record/priv.h"

static int bind_udp_loopback(unsigned port) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static unsigned free_even_udp_port_pair(void) {
  for (int tries = 0; tries < 100; tries++) {
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int a = bind_udp_loopback(0);
    unsigned port;
    int b;

    ck_assert_int_ge(a, 0);
    ck_assert_int_eq(getsockname(a, (struct sockaddr *)&addr, &alen), 0);
    port = ntohs(addr.sin_port) & ~1u;
    close(a);
    a = bind_udp_loopback(port);
    b = bind_udp_loopback(port + 1);
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    if (a >= 0 && b >= 0) return port;
  }
  ck_abort_msg("no free even/odd udp port pair");
  return 0;
}

START_TEST(src_open_listens_for_rist) {
  config_t cfg;
  src_t s;
  char uri[64];

  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%u", free_even_udp_port_pair());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_int_eq(s.kind, URI_RIST);
  ck_assert_int_eq(src_wait_readable(&s, 0), 0);
  src_close(&s);
}
END_TEST

START_TEST(src_open_listens_for_srt) {
  config_t cfg;
  src_t s;
  char uri[64];

  snprintf(uri, sizeof uri, "srt://@127.0.0.1:%u", free_even_udp_port_pair());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  ck_assert_int_eq(s.kind, URI_SRT);
  ck_assert_int_eq(src_wait_readable(&s, 0), 0);
  src_close(&s);
}
END_TEST

START_TEST(src_open_srt_with_unknown_passphrase_options_still_listens) {
  config_t cfg;
  src_t s;
  char uri[64];

  snprintf(uri, sizeof uri, "srt://@127.0.0.1:%u", free_even_udp_port_pair());
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, uri), 0);
  snprintf(cfg.srt_passphrase_in, sizeof cfg.srt_passphrase_in, "0123456789ab");
  cfg.srt_pbkeylen_in = 16;
  snprintf(cfg.srt_streamid_in, sizeof cfg.srt_streamid_in, "sid");
  cfg.srt_latency_in_ms = 120;
  ck_assert_int_eq(src_open(&cfg, &s), 0);
  src_close(&s);
}
END_TEST

#define LINK_DEADLINE_S 10.0
#define CHUNKS 5
#define CHUNK_BYTES 1316

static double now_seconds(void) {
  struct timespec ts;

  ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void fill_payload(unsigned char *buf, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = (unsigned char)(i * 13 + 5);
}

START_TEST(srt_sink_delivers_to_a_listening_source) {
  const size_t total = (size_t)CHUNKS * CHUNK_BYTES;
  config_t cfg;
  src_t src;
  out_sink_t o;
  char in_uri[64];
  char out_uri[64];
  unsigned port = free_even_udp_port_pair();
  unsigned char *payload = malloc(total);
  unsigned char *rx = malloc(total);
  size_t got = 0;
  int sent = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;

  ck_assert_ptr_nonnull(payload);
  ck_assert_ptr_nonnull(rx);
  fill_payload(payload, total);
  snprintf(in_uri, sizeof in_uri, "srt://@127.0.0.1:%u", port);
  snprintf(out_uri, sizeof out_uri, "srt://127.0.0.1:%u", port);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, in_uri), 0);
  ck_assert_int_eq(src_open(&cfg, &src), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, out_uri), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  ck_assert_int_eq(o.kind, OUT_SRT);

  while (got < total && now_seconds() < deadline) {
    ssize_t n;

    sinks_service_srt(&o, 1);
    if (o.srt_connected && !sent) {
      ck_assert_int_eq(sink_write(&o, payload, total), 0);
      sent = 1;
    }
    if (src_wait_readable(&src, 20) != 1) continue;
    n = src_read(&src, rx + got, total - got);
    if (n < 0) break;
    got += (size_t)n;
  }
  ck_assert_uint_eq(got, total);
  ck_assert_mem_eq(rx, payload, total);
  sink_close(&o);
  src_close(&src);
  free(payload);
  free(rx);
}
END_TEST

#ifndef DVBIPITOOLS_TSAN_BUILD
START_TEST(rist_sink_delivers_to_a_listening_source) {
  const size_t total = (size_t)CHUNKS * CHUNK_BYTES;
  config_t cfg;
  src_t src;
  out_sink_t o;
  char in_uri[64];
  char out_uri[64];
  unsigned port = free_even_udp_port_pair();
  unsigned char *payload = malloc(total);
  unsigned char *rx = malloc(total);
  size_t got = 0;
  double deadline = now_seconds() + LINK_DEADLINE_S;
  double next_send = 0.0;

  ck_assert_ptr_nonnull(payload);
  ck_assert_ptr_nonnull(rx);
  fill_payload(payload, total);
  snprintf(in_uri, sizeof in_uri, "rist://@127.0.0.1:%u", port);
  snprintf(out_uri, sizeof out_uri, "rist://127.0.0.1:%u", port);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, in_uri), 0);
  ck_assert_int_eq(src_open(&cfg, &src), 0);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, out_uri), 0);
  ck_assert_int_eq(sink_open(&cfg, &cfg.out[0], &o), 0);
  ck_assert_int_eq(o.kind, OUT_RIST);

  while (got < total && now_seconds() < deadline) {
    ssize_t n;

    if (now_seconds() >= next_send) {
      ck_assert_int_eq(sink_write(&o, payload, CHUNK_BYTES), 0);
      next_send = now_seconds() + 0.05;
    }
    if (src_wait_readable(&src, 20) != 1) continue;
    n = src_read(&src, rx + got, total - got);
    if (n < 0) break;
    got += (size_t)n;
  }
  ck_assert_uint_eq(got, total);
  for (size_t off = 0; off < total; off += CHUNK_BYTES) ck_assert_mem_eq(rx + off, payload, CHUNK_BYTES);
  sink_close(&o);
  src_close(&src);
  free(payload);
  free(rx);
}
END_TEST
#endif

static Suite *record_net_suite(void) {
  Suite *s = suite_create("dipirec_record_net");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, src_open_listens_for_rist);
  tcase_add_test(tc, src_open_listens_for_srt);
  tcase_add_test(tc, src_open_srt_with_unknown_passphrase_options_still_listens);
  tcase_add_test(tc, srt_sink_delivers_to_a_listening_source);
#ifndef DVBIPITOOLS_TSAN_BUILD
  tcase_add_test(tc, rist_sink_delivers_to_a_listening_source);
#endif
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(record_net_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
