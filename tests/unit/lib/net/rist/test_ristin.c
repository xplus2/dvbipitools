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
#include <sys/wait.h>
#include <unistd.h>

#include "lib/net/rist/ristin.h"
#include "lib/net/rist/ristin_priv.h"
#include "../../../metrics_sink.h"

#ifndef RIST_SEND_HELPER_PATH
#error "RIST_SEND_HELPER_PATH must be defined to the built rist_send_helper binary's path"
#endif


START_TEST(rejects_non_rist_scheme) {
  ristin_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peer_uri = "udp://@127.0.0.1:15982";
  ck_assert_ptr_null(ristin_open(&cfg));
}
END_TEST

START_TEST(rejects_missing_at) {
  ristin_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peer_uri = "rist://127.0.0.1:15982"; /* dial, not listen: wrong role for input */
  ck_assert_ptr_null(ristin_open(&cfg));
}
END_TEST

START_TEST(rejects_a_listen_port_whose_rtcp_sibling_is_taken) {
  ristin_cfg_t cfg;
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  char uri[64];
  int fd = -1;

  for (int tries = 0; tries < 100; tries++) {
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ck_assert_int_ge(fd, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
    ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
    if (ntohs(a.sin_port) & 1u) break;
    close(fd);
    fd = -1;
  }
  ck_assert_int_ge(fd, 0);
  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%u", ntohs(a.sin_port) - 1u);
  memset(&cfg, 0, sizeof cfg);
  cfg.peer_uri = uri;
  ck_assert_ptr_null(ristin_open(&cfg));
  close(fd);
}
END_TEST

static struct rist_stats *new_stats(enum rist_stats_type type) {
  struct rist_stats *st = calloc(1, sizeof *st);

  ck_assert_ptr_nonnull(st);
  st->stats_type = type;
  return st;
}

START_TEST(receiver_stats_cb_pushes_flow_counters) {
  sink_t s;
  seen_t seen;
  ristin_t r;
  struct rist_stats *st = new_stats(RIST_STATS_RECEIVER_FLOW);
  uint64_t v = 0;

  st->stats.receiver_flow.received = 1000;
  st->stats.receiver_flow.missing = 7;
  st->stats.receiver_flow.recovered = 5;
  st->stats.receiver_flow.lost = 2;
  st->stats.receiver_flow.rtt = 33;
  sink_open(&s, METRICS_COMPONENT_RIST, "rist1", 5.0);
  memset(&r, 0, sizeof r);
  r.mx = &s.mx;
  r.tool_version = "test";
  ck_assert_int_eq(receiver_stats_cb(&r, st), 0);
  ck_assert_int_eq(sink_read(&s, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1000u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 7u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 5u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_LOST_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, &v), 1);
  ck_assert_uint_eq(v, 33u);
  sink_close(&s);
}
END_TEST

START_TEST(receiver_stats_cb_ignores_other_stats_types) {
  sink_t s;
  seen_t seen;
  ristin_t r;
  struct rist_stats *st = new_stats(RIST_STATS_SENDER_PEER);

  sink_open(&s, METRICS_COMPONENT_RIST, "rist1", 5.0);
  memset(&r, 0, sizeof r);
  r.mx = &s.mx;
  r.tool_version = "test";
  ck_assert_int_eq(receiver_stats_cb(&r, st), 0);
  ck_assert_int_eq(sink_read(&s, &seen), 0);
  sink_close(&s);
}
END_TEST

#ifndef DVBIPITOOLS_TSAN_BUILD
/* excluded under TSAN: race is inside librist's receiver impl */

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

/* blocking pipe read end: bound wait so a delivery failure fails test instead of hanging */
static ssize_t read_with_timeout(int fd, void *buf, size_t cap, int timeout_ms) {
  struct pollfd pfd = {.fd = fd, .events = POLLIN};

  if (poll(&pfd, 1, timeout_ms) <= 0)
    return -1;
  return read(fd, buf, cap);
}

START_TEST(receives_payload_from_a_real_rist_sender) {
  ristin_cfg_t cfg;
  ristin_t *r;
  char uri[64];
  const char *payload = "hello from a real rist sender";
  char buf[128];
  ssize_t got;
  pid_t pid;
  int status;
  unsigned port = free_even_udp_port_pair();

  /* fork before ristin_open(): forking a threaded process is itself a
     sanitizer hazard. receiver's reader thread must not exist at fork() time */
  pid = fork();
  ck_assert_int_ge(pid, 0);
  if (pid == 0) {
    char sender_uri[64];
    snprintf(sender_uri, sizeof sender_uri, "rist://127.0.0.1:%u", port);
    execl(RIST_SEND_HELPER_PATH, RIST_SEND_HELPER_PATH, sender_uri, payload, (char *)NULL);
    _exit(127); /* exec failed */
  }

  memset(&cfg, 0, sizeof cfg);
  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%u", port);
  cfg.peer_uri = uri;
  r = ristin_open(&cfg);
  ck_assert_ptr_nonnull(r);

  got = read_with_timeout(ristin_fd(r), buf, sizeof buf - 1, 2000);
  ck_assert_int_eq(got, (ssize_t)strlen(payload));
  buf[got] = '\0';
  ck_assert_str_eq(buf, payload);

  ck_assert_int_eq(waitpid(pid, &status, 0), pid);
  ck_assert_int_eq(WIFEXITED(status) ? WEXITSTATUS(status) : -1, 0);

  ristin_close(r);
}
END_TEST
#endif

static Suite *ristin_suite(void) {
  Suite *s = suite_create("ristin");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rejects_non_rist_scheme);
  tcase_add_test(tc, rejects_missing_at);
  tcase_add_test(tc, rejects_a_listen_port_whose_rtcp_sibling_is_taken);
  tcase_add_test(tc, receiver_stats_cb_pushes_flow_counters);
  tcase_add_test(tc, receiver_stats_cb_ignores_other_stats_types);
#ifndef DVBIPITOOLS_TSAN_BUILD
  tcase_add_test(tc, receives_payload_from_a_real_rist_sender);
#endif
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ristin_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
