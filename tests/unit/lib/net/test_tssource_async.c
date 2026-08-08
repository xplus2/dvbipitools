/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/net/tssource.h"

static tssrc_open_state_t drive(tssrc_open_t *o, int max_iters) {
  tssrc_open_state_t st = TSSRC_OPEN_PENDING;
  int i;

  for (i = 0; i < max_iters && st == TSSRC_OPEN_PENDING; i++) {
    struct pollfd pfd;
    int fd = tssrc_open_async_poll_fd(o);
    if (fd < 0) {
      st = tssrc_open_async_step(o, NULL);
      continue;
    }
    pfd.fd = fd;
    pfd.events = tssrc_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = tssrc_open_async_step(o, NULL);
  }
  return st;
}

START_TEST(tssrc_open_async_completes_immediately_for_udp) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_UDP;
  cfg.family = AF_INET;
  cfg.group = "239.1.5.5";
  cfg.port = 15005;

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 5), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fd(s), 0);

  tssrc_close(s);
}
END_TEST

START_TEST(tssrc_open_async_completes_immediately_for_stdin) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_STDIN;

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 5), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tssrc_fd(s), STDIN_FILENO);

  tssrc_close(s);
}
END_TEST

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  char buf[4096];
  if (cfd < 0)
    return NULL;
  recv(cfd, buf, sizeof buf, 0);
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

START_TEST(tssrc_open_async_completes_for_http_and_reads_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\nTSBYTES";
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  char buf[64], uri[64];
  size_t got = 0;
  int tries = 0;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), TSSRC_OPEN_DONE);

  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tssrc_fd(s), 0);

  while (got < strlen("TSBYTES") && tries++ < 100) {
    ssize_t n = tssrc_read(s, (unsigned char *)buf + got, sizeof buf - 1 - got, NULL);
    if (n > 0)
      got += (size_t)n;
    else if (n < 0)
      break;
    else
      usleep(5000);
  }
  buf[got] = '\0';
  ck_assert_str_eq(buf, "TSBYTES");

  tssrc_close(s);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(tssrc_open_async_reports_error_on_refused_connection) {
  tssrc_cfg_t cfg;
  tssrc_open_t *o;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/nothing", &cfg.http), 0); /* port 1: nothing listens here */

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), TSSRC_OPEN_ERROR);
  tssrc_open_async_free(o);
}
END_TEST

static Suite *tssource_async_suite(void) {
  Suite *s = suite_create("tssource_async");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tssrc_open_async_completes_immediately_for_udp);
  tcase_add_test(tc, tssrc_open_async_completes_immediately_for_stdin);
  tcase_add_test(tc, tssrc_open_async_completes_for_http_and_reads_body);
  tcase_add_test(tc, tssrc_open_async_reports_error_on_refused_connection);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssource_async_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
