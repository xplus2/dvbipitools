/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/net/ts/source.h"
#include "lib/net/ts/source_priv.h"

#define PKT 188

static void fill_pkts(unsigned char *out, unsigned n, unsigned char first_tag) {
  for (unsigned i = 0; i < n; i++) {
    memset(out + i * PKT, (int)(first_tag + i), PKT);
    out[i * PKT] = 0x47;
  }
}

static char *write_temp(const unsigned char *data, size_t len) {
  char tmpl[] = "/tmp/dvbipitools_tssrc_src_XXXXXX";
  char *p = strdup(tmpl);
  int fd = mkstemp(p);
  ck_assert_int_ge(fd, 0);
  if (len) ck_assert_int_eq(write(fd, data, len), (ssize_t)len);
  close(fd);
  return p;
}

static tssrc_t *open_file(const char *path, net_err_reason_t *reason) {
  tssrc_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = path;
  return tssrc_open(&cfg, reason);
}

static size_t read_all_pkts(tssrc_t *s, unsigned char *out, size_t cap, size_t chunk) {
  size_t got = 0;
  for (int i = 0; i < 1000; i++) {
    ssize_t n = tssrc_read(s, out + got, got + chunk <= cap ? chunk : cap - got, NULL);
    if (n < 0) break;
    got += (size_t)n;
  }
  return got;
}

START_TEST(file_leading_junk_is_skipped) {
  unsigned char data[37 + 5 * PKT];
  unsigned char out[8 * PKT];
  char *path;
  tssrc_t *s;
  size_t got;
  memset(data, 0x11, 37);
  fill_pkts(data + 37, 5, 1);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  got = read_all_pkts(s, out, sizeof out, 4 * PKT);
  ck_assert_uint_eq(got % PKT, 0u);
  ck_assert_uint_ge(got, 4 * PKT);
  ck_assert_uint_eq(out[0], 0x47u);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_resyncs_after_garbage_in_the_middle) {
  unsigned char data[3 * PKT + 400 + 4 * PKT];
  unsigned char out[16 * PKT];
  char *path;
  tssrc_t *s;
  size_t got;
  fill_pkts(data, 3, 1);
  memset(data + 3 * PKT, 0x22, 400);
  fill_pkts(data + 3 * PKT + 400, 4, 9);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  got = read_all_pkts(s, out, sizeof out, 6 * PKT);
  ck_assert_uint_eq(got % PKT, 0u);
  for (size_t i = 0; i < got; i += PKT) ck_assert_uint_eq(out[i], 0x47u);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_without_any_sync_yields_nothing) {
  unsigned char data[1500];
  unsigned char out[8 * PKT];
  char *path;
  tssrc_t *s;
  memset(data, 0x33, sizeof data);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_uint_eq(read_all_pkts(s, out, sizeof out, 4 * PKT), 0u);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_partial_tail_is_dropped_at_eof) {
  unsigned char data[3 * PKT + 100];
  unsigned char out[8 * PKT];
  char *path;
  tssrc_t *s;
  fill_pkts(data, 3, 1);
  memset(data + 3 * PKT, 0x47, 100);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_uint_eq(read_all_pkts(s, out, sizeof out, 8 * PKT), 3u * PKT);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_small_read_buffers_bypass_alignment) {
  unsigned char data[4 * PKT];
  unsigned char out[PKT];
  char *path;
  tssrc_t *s;
  fill_pkts(data, 4, 1);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  for (int i = 0; i < 5; i++) ck_assert_int_ge(tssrc_read(s, out, sizeof out, NULL), 0);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_open_failure_reports_other) {
  net_err_reason_t reason = NET_ERR_COUNT;
  ck_assert_ptr_null(open_file("/nonexistent/dir/x.ts", &reason));
  ck_assert_int_eq(reason, NET_ERR_OTHER);
  ck_assert_ptr_null(open_file("/nonexistent/dir/x.ts", NULL));
}
END_TEST

START_TEST(file_rewind_restarts_the_stream) {
  unsigned char data[4 * PKT];
  unsigned char a[8 * PKT];
  unsigned char b[8 * PKT];
  char *path;
  tssrc_t *s;
  size_t na;
  size_t nb;
  fill_pkts(data, 4, 1);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  na = read_all_pkts(s, a, sizeof a, 4 * PKT);
  ck_assert_int_eq(tssrc_rewind(s), 1);
  nb = read_all_pkts(s, b, sizeof b, 4 * PKT);
  ck_assert_uint_eq(na, nb);
  ck_assert_int_eq(memcmp(a, b, na), 0);
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

START_TEST(file_wait_and_fd_accessors) {
  unsigned char data[2 * PKT];
  char *path;
  tssrc_t *s;
  fill_pkts(data, 2, 1);
  path = write_temp(data, sizeof data);
  s = open_file(path, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fd(s), 0);
  ck_assert_int_eq(tssrc_poll_events(s), POLLIN);
  ck_assert_int_eq(tssrc_timer_driven(s), 0);
  ck_assert_int_eq(tssrc_wait(s, 100), 1);
  ck_assert_int_eq(tssrc_enable_rx_timestamps(s), -1);
  ck_assert_uint_eq((unsigned)tssrc_last_rx_ns(s), 0u);
  ck_assert_int_eq(tssrc_set_nonblock(s), 0);
  ck_assert_int_eq(tssrc_fec_fd(s), -1);
  tssrc_fec_poll(s);
  ck_assert_ptr_null(tssrc_mcast(s));
  ck_assert_int_eq(tssrc_src_fd(s), tssrc_fd(s));
  tssrc_close(s);
  unlink(path);
  free(path);
}
END_TEST

static tssrc_t *open_stdin_pipe(int *wfd, int *saved) {
  int fds[2];
  tssrc_cfg_t cfg;
  ck_assert_int_eq(pipe(fds), 0);
  *saved = dup(STDIN_FILENO);
  ck_assert_int_eq(dup2(fds[0], STDIN_FILENO), STDIN_FILENO);
  close(fds[0]);
  *wfd = fds[1];
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_STDIN;
  return tssrc_open(&cfg, NULL);
}

START_TEST(stdin_capture_and_replay) {
  unsigned char data[6 * PKT];
  unsigned char out[8 * PKT];
  unsigned char again[8 * PKT] = {0};
  int wfd;
  int saved;
  tssrc_t *s = open_stdin_pipe(&wfd, &saved);
  size_t first;
  size_t replay = 0;
  ck_assert_ptr_nonnull(s);
  fill_pkts(data, 6, 1);
  ck_assert_int_eq(write(wfd, data, sizeof data), (ssize_t)sizeof data);
  close(wfd);
  tssrc_capture_start(s);
  ck_assert_int_eq(tssrc_replay_pending(s), 0);
  ck_assert_int_eq(tssrc_wait(s, 500), 1);
  first = 0;
  for (int i = 0; i < 20 && !first; i++) first = (size_t)tssrc_read(s, out, sizeof out, NULL);
  ck_assert_uint_gt(first, 0u);
  ck_assert_int_eq(tssrc_rewind(s), 1);
  ck_assert_int_eq(tssrc_replay_pending(s), 1);
  ck_assert_int_eq(tssrc_wait(s, 0), 1);
  while (tssrc_replay_pending(s)) {
    ssize_t n = tssrc_read(s, again + replay, 300, NULL);
    ck_assert_int_gt(n, 0);
    replay += (size_t)n;
  }
  ck_assert_uint_eq(replay, first);
  ck_assert_int_eq(memcmp(out, again, first), 0);
  tssrc_close(s);
  dup2(saved, STDIN_FILENO);
  close(saved);
}
END_TEST

START_TEST(stdin_rewind_without_capture_fails) {
  int wfd;
  int saved;
  tssrc_t *s = open_stdin_pipe(&wfd, &saved);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssrc_rewind(s), 0);
  tssrc_capture_start(s);
  ck_assert_int_eq(tssrc_rewind(s), 0);
  tssrc_close(s);
  close(wfd);
  dup2(saved, STDIN_FILENO);
  close(saved);
}
END_TEST

START_TEST(stdin_capture_stops_when_it_exceeds_the_limit) {
  unsigned char data[8 * PKT];
  unsigned char out[16 * PKT];
  int wfd;
  int saved;
  tssrc_t *s = open_stdin_pipe(&wfd, &saved);
  ck_assert_ptr_nonnull(s);
  fill_pkts(data, 8, 1);
  ck_assert_int_eq(write(wfd, data, sizeof data), (ssize_t)sizeof data);
  close(wfd);
  tssrc_capture_start(s);
  s->cap_max = 400;
  for (int i = 0; i < 10; i++) (void)tssrc_read(s, out, sizeof out, NULL);
  ck_assert_int_eq(tssrc_rewind(s), 0);
  tssrc_close(s);
  dup2(saved, STDIN_FILENO);
  close(saved);
}
END_TEST
static void unique_group(char *out, size_t cap) {
  snprintf(out, cap, "239.78.%u.%u", ((unsigned)getpid() >> 8) & 0xFF, 1 + ((unsigned)getpid() & 0xFF) % 250);
}

static unsigned free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

START_TEST(udp_read_wait_and_accessors) {
  tssrc_cfg_t cfg;
  tssrc_t *s;
  char group[32];
  struct sockaddr_in dst;
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned char pkt[12 + PKT];
  unsigned char out[2048];
  unique_group(group, sizeof group);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = group;
  cfg.port = free_udp_port();
  s = tssrc_open(&cfg, NULL);
  ck_assert_ptr_nonnull(s);
  ck_assert_ptr_nonnull(tssrc_mcast(s));
  ck_assert_int_eq(tssrc_wait(s, 20), 0);
  ck_assert_int_eq(tssrc_set_nonblock(s), 1);
  ck_assert_int_eq(tssrc_enable_rx_timestamps(s), 0);
  ck_assert_int_eq(tssrc_fec_fd(s), -1);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)cfg.port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &dst.sin_addr), 1);
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  ck_assert_int_eq((int)sendto(sock, pkt, PKT, 0, (struct sockaddr *)&dst, sizeof dst), PKT);
  ck_assert_int_eq(tssrc_wait(s, 1000), 1);
  ck_assert_int_eq((int)tssrc_read(s, out, sizeof out, NULL), PKT);
  ck_assert_uint_gt((unsigned)tssrc_last_rx_ns(s), 0u);
  close(sock);
  tssrc_close(s);
}
END_TEST

START_TEST(unsupported_open_configurations_fail_cleanly) {
  tssrc_cfg_t cfg;
  net_err_reason_t reason = NET_ERR_COUNT;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = "not-a-group";
  cfg.port = 5000;
  ck_assert_ptr_null(tssrc_open(&cfg, &reason));
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_FILE;
  cfg.file_path = "/nonexistent/x.ts";
  cfg.jitter_ms = 50;
  ck_assert_ptr_null(tssrc_open(&cfg, NULL));
}
END_TEST

static Suite *source_suite(void) {
  Suite *s = suite_create("source");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, file_leading_junk_is_skipped);
  tcase_add_test(tc, file_resyncs_after_garbage_in_the_middle);
  tcase_add_test(tc, file_without_any_sync_yields_nothing);
  tcase_add_test(tc, file_partial_tail_is_dropped_at_eof);
  tcase_add_test(tc, file_small_read_buffers_bypass_alignment);
  tcase_add_test(tc, file_open_failure_reports_other);
  tcase_add_test(tc, file_rewind_restarts_the_stream);
  tcase_add_test(tc, file_wait_and_fd_accessors);
  tcase_add_test(tc, stdin_capture_and_replay);
  tcase_add_test(tc, stdin_rewind_without_capture_fails);
  tcase_add_test(tc, stdin_capture_stops_when_it_exceeds_the_limit);
  tcase_add_test(tc, udp_read_wait_and_accessors);
  tcase_add_test(tc, unsupported_open_configurations_fail_cleanly);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(source_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
