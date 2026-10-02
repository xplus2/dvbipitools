/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipitvhead/input/source.h"

static void wait_ms(int ms) {
  struct timespec ts = {0, (long)ms * 1000000L};
  nanosleep(&ts, NULL);
}

static ssize_t pump_datagram(tvsrc_t *s, const char *group, unsigned port, const unsigned char *pkt, size_t pkt_len, unsigned char *rbuf, size_t rcap) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in dst;
  net_err_reason_t reason = NET_ERR_OTHER;
  ssize_t n = -1;

  ck_assert_int_ge(sock, 0);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst.sin_addr);
  for (int i = 0; i < 20 && n < 0; i++) {
    sendto(sock, pkt, pkt_len, 0, (const struct sockaddr *)&dst, sizeof dst);
    wait_ms(20);
    n = tvsrc_read(s, rbuf, rcap, &reason);
  }
  close(sock);
  return n;
}

static unsigned free_udp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static void fill_udp_input(dipitvhead_input_t *in, const char *group, unsigned port) {
  memset(in, 0, sizeof *in);
  in->input.kind = SRC_UDP;
  in->input.family = AF_INET;
  strcpy(in->input.group, group);
  in->input.port = port;
}

START_TEST(udp_kind_opens_a_real_socket_and_reads_datagrams) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;
  unsigned port = free_udp_port();
  unsigned char pkt[188];
  unsigned char rbuf[512];
  ssize_t n;

  memset(&cfg, 0, sizeof cfg);
  fill_udp_input(&in, "239.7.9.31", port);
  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tvsrc_fd(s), 0);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  n = pump_datagram(s, "239.7.9.31", port, pkt, sizeof pkt, rbuf, sizeof rbuf);
  ck_assert_int_eq(n, (ssize_t)sizeof pkt);
  ck_assert_mem_eq(rbuf, pkt, sizeof pkt);

  tvsrc_close(s);
}
END_TEST

START_TEST(stdin_kind_maps_to_the_stdin_fd) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_STDIN;

  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tvsrc_fd(s), STDIN_FILENO);

  tvsrc_close(s);
}
END_TEST

START_TEST(http_kind_with_unreachable_host_fails_to_open) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_HTTP;
  strcpy(in.input.http.host, "127.0.0.1");
  in.input.http.port = 1; /* nothing listens on port 1 */
  strcpy(in.input.http.path, "/");

  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_null(s);
}
END_TEST

START_TEST(rist_and_srt_kinds_fail_cleanly_without_their_backends) {
  config_t cfg;
  dipitvhead_input_t in;
  net_err_reason_t reason = NET_ERR_COUNT;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  if (_i == 0) {
    in.input.kind = SRC_RIST;
    strcpy(in.input.rist_uri, "rist://@127.0.0.1:19000");
  } else {
    in.input.kind = SRC_SRT;
    strcpy(in.input.srt_host, "127.0.0.1");
    in.input.srt_port = 19001;
    in.input.srt_listen = 1;
  }
  ck_assert_ptr_null(tvsrc_open(&cfg, &in, &reason));
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
}
END_TEST

START_TEST(async_rist_and_srt_kinds_fail_before_any_polling) {
  config_t cfg;
  dipitvhead_input_t in;
  net_err_reason_t reason = NET_ERR_COUNT;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = _i == 0 ? SRC_RIST : SRC_SRT;
  strcpy(in.input.rist_uri, "rist://@127.0.0.1:19000");
  strcpy(in.input.srt_host, "127.0.0.1");
  in.input.srt_port = 19001;
  ck_assert_ptr_null(tvsrc_open_async_start(&cfg, &in, &reason));
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
}
END_TEST

START_TEST(async_udp_open_completes_on_first_step_and_reads) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_open_t *o;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_COUNT;
  unsigned port = free_udp_port();
  unsigned char pkt[188];
  unsigned char rbuf[512];

  memset(&cfg, 0, sizeof cfg);
  fill_udp_input(&in, "239.7.9.32", port);
  o = tvsrc_open_async_start(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(tvsrc_open_async_step(o, &reason), TVSRC_OPEN_DONE);
  s = tvsrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  ck_assert_int_eq(pump_datagram(s, "239.7.9.32", port, pkt, sizeof pkt, rbuf, sizeof rbuf), (ssize_t)sizeof pkt);
  tvsrc_close(s);
}
END_TEST

typedef struct {
  int listen_fd;
  unsigned port;
  const char *response;
  size_t response_len;
  atomic_int release;
  pthread_t thread;
} http_server_t;

static void *http_server_main(void *arg) {
  const http_server_t *h = arg;
  struct pollfd pfd = {h->listen_fd, POLLIN, 0};
  int cfd;
  char buf[2048];
  size_t got = 0;

  if (poll(&pfd, 1, 5000) <= 0) return NULL;
  cfd = accept(h->listen_fd, NULL, NULL);
  if (cfd < 0) return NULL;
  while (got < sizeof buf - 1) {
    struct pollfd cp = {cfd, POLLIN, 0};
    ssize_t r;

    if (poll(&cp, 1, 2000) <= 0) break;
    r = recv(cfd, buf + got, sizeof buf - 1 - got, 0);
    if (r <= 0) break;
    got += (size_t)r;
    if (got >= 4 && memmem(buf, got, "\r\n\r\n", 4)) break;
  }
  if (h->response_len) send(cfd, h->response, h->response_len, MSG_NOSIGNAL);
  while (!atomic_load(&h->release)) wait_ms(10);
  close(cfd);
  return NULL;
}

static void http_server_start(http_server_t *h, const char *response, size_t response_len) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(h, 0, sizeof *h);
  h->response = response;
  h->response_len = response_len;
  h->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(h->listen_fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(h->listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(h->listen_fd, 2), 0);
  ck_assert_int_eq(getsockname(h->listen_fd, (struct sockaddr *)&addr, &alen), 0);
  h->port = ntohs(addr.sin_port);
  ck_assert_int_eq(pthread_create(&h->thread, NULL, http_server_main, h), 0);
}

static void http_server_stop(http_server_t *h) {
  atomic_store(&h->release, 1);
  pthread_join(h->thread, NULL);
  close(h->listen_fd);
}

static void fill_http_input(dipitvhead_input_t *in, unsigned port) {
  memset(in, 0, sizeof *in);
  in->input.kind = SRC_HTTP;
  strcpy(in->input.http.host, "127.0.0.1");
  in->input.http.port = port;
  strcpy(in->input.http.path, "/ts");
}

static tvsrc_open_state_t drive_open(tvsrc_open_t *o, int rounds, net_err_reason_t *reason) {
  tvsrc_open_state_t st = TVSRC_OPEN_PENDING;

  for (int i = 0; i < rounds && st == TVSRC_OPEN_PENDING; i++) {
    struct pollfd pfd;

    pfd.fd = tvsrc_open_async_poll_fd(o);
    pfd.events = tvsrc_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 50);
    st = tvsrc_open_async_step(o, reason);
  }
  return st;
}

START_TEST(async_http_open_reports_connection_refused) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_open_t *o;
  net_err_reason_t reason = NET_ERR_COUNT;

  memset(&cfg, 0, sizeof cfg);
  fill_http_input(&in, 1);
  o = tvsrc_open_async_start(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 100, &reason), TVSRC_OPEN_ERROR);
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
  tvsrc_open_async_free(o);
}
END_TEST

START_TEST(async_http_open_delivers_a_transport_stream_body) {
  static char response[2048];
  config_t cfg;
  dipitvhead_input_t in;
  http_server_t srv;
  tvsrc_open_t *o;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_COUNT;
  unsigned char rbuf[4096];
  size_t header_len;
  size_t body_len = 188 * 8;
  size_t got = 0;

  header_len = (size_t)snprintf(response, sizeof response, "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", body_len);
  for (size_t i = 0; i < body_len; i++) response[header_len + i] = (i % 188 == 0) ? 0x47 : (char)(i * 3);
  http_server_start(&srv, response, header_len + body_len);

  memset(&cfg, 0, sizeof cfg);
  fill_http_input(&in, srv.port);
  o = tvsrc_open_async_start(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 100, &reason), TVSRC_OPEN_DONE);
  s = tvsrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  for (int i = 0; i < 100 && got < body_len; i++) {
    ssize_t n = tvsrc_read(s, rbuf + got, sizeof rbuf - got, &reason);

    ck_assert_int_ge((int)n, 0);
    got += (size_t)n;
    if (n == 0) wait_ms(10);
  }
  ck_assert_uint_eq(got, body_len);
  ck_assert_int_eq(rbuf[0], 0x47);
  ck_assert_int_eq(rbuf[188], 0x47);
  tvsrc_close(s);
  http_server_stop(&srv);
}
END_TEST

START_TEST(async_http_open_rejects_unrecognized_content) {
  static const char response[] = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 11\r\n\r\nhello world";
  config_t cfg;
  dipitvhead_input_t in;
  http_server_t srv;
  tvsrc_open_t *o;
  net_err_reason_t reason = NET_ERR_COUNT;

  http_server_start(&srv, response, sizeof response - 1);
  memset(&cfg, 0, sizeof cfg);
  fill_http_input(&in, srv.port);
  o = tvsrc_open_async_start(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 100, &reason), TVSRC_OPEN_ERROR);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  tvsrc_open_async_free(o);
  http_server_stop(&srv);
}
END_TEST

START_TEST(async_http_open_stays_pending_against_a_silent_server_and_frees_cleanly) {
  config_t cfg;
  dipitvhead_input_t in;
  http_server_t srv;
  tvsrc_open_t *o;
  net_err_reason_t reason = NET_ERR_COUNT;

  http_server_start(&srv, "", 0);
  memset(&cfg, 0, sizeof cfg);
  fill_http_input(&in, srv.port);
  o = tvsrc_open_async_start(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 6, &reason), TVSRC_OPEN_PENDING);
  tvsrc_open_async_free(o);
  http_server_stop(&srv);
}
END_TEST

START_TEST(async_free_accepts_null) {
  tvsrc_open_async_free(NULL);
}
END_TEST

static Suite *source_suite(void) {
  Suite *s = suite_create("dipitvhead_source");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, udp_kind_opens_a_real_socket_and_reads_datagrams);
  tcase_add_test(tc, stdin_kind_maps_to_the_stdin_fd);
  tcase_add_test(tc, http_kind_with_unreachable_host_fails_to_open);
  tcase_add_loop_test(tc, rist_and_srt_kinds_fail_cleanly_without_their_backends, 0, 2);
  tcase_add_loop_test(tc, async_rist_and_srt_kinds_fail_before_any_polling, 0, 2);
  tcase_add_test(tc, async_udp_open_completes_on_first_step_and_reads);
  tcase_add_test(tc, async_http_open_reports_connection_refused);
  tcase_add_test(tc, async_http_open_delivers_a_transport_stream_body);
  tcase_add_test(tc, async_http_open_rejects_unrecognized_content);
  tcase_add_test(tc, async_http_open_stays_pending_against_a_silent_server_and_frees_cleanly);
  tcase_add_test(tc, async_free_accepts_null);
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
