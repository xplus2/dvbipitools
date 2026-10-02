/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include "../tls_fixture.h"
#include "client_resp.h"
#include "dipixy/core/htdocs.h"
#include "dipixy/core/metrics.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/reactor.h"
#include "dipixy/ts/channels/channels.h"
#include "lib/sys/signal.h"

#define REPLY_MAX 4096
#define WAIT_MS 5000
#define ARGV_MAX 16

typedef struct {
  config_t cfg;
  channels_t *channels;
  metrics_exporter_t mx;
  pthread_t th;
  int rc;
  char dir[64];
  char cert[96];
  char key[96];
  char listen_arg[40];
  char tls_arg[40];
  char extra_arg[64];
  unsigned port;
  unsigned tls_port;
} run_t;

static _Atomic int g_listening;
static _Atomic(const config_t *) g_listening_cfg;

static void on_listening(const config_t *cfg) {
  atomic_store(&g_listening_cfg, cfg);
  atomic_store(&g_listening, 1);
}

static unsigned free_tcp_port(void) {
  struct sockaddr_in a = {0};
  socklen_t alen = sizeof a;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &alen), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static void *run_main(void *arg) {
  run_t *r = arg;

  r->rc = reactor_run(&r->cfg, r->channels, &r->mx, on_listening);
  return NULL;
}

static void run_start(run_t *r, int with_tls, const char *extra) {
  char *argv[ARGV_MAX];
  int argc = 0;
  struct timespec ts = {0, 5000000};

  memset(r, 0, sizeof *r);
  atomic_store(&g_listening, 0);
  r->port = free_tcp_port();
  snprintf(r->listen_arg, sizeof r->listen_arg, "127.0.0.1:%u", r->port);
  argv[argc++] = "dipixy";
  argv[argc++] = "-l";
  argv[argc++] = r->listen_arg;
  argv[argc++] = "-j";
  argv[argc++] = "1";
  argv[argc++] = "--no-http3";
  if (with_tls) {
    snprintf(r->dir, sizeof r->dir, "/tmp/dipixy_run_XXXXXX");
    ck_assert_ptr_nonnull(mkdtemp(r->dir));
    snprintf(r->cert, sizeof r->cert, "%s/s.crt", r->dir);
    snprintf(r->key, sizeof r->key, "%s/s.key", r->dir);
    tls_fixture_write_cert(r->cert, r->key);
    r->tls_port = free_tcp_port();
    snprintf(r->tls_arg, sizeof r->tls_arg, "127.0.0.1:%u", r->tls_port);
    argv[argc++] = "-L";
    argv[argc++] = r->tls_arg;
    argv[argc++] = "--tls-cert";
    argv[argc++] = r->cert;
    argv[argc++] = "--tls-key";
    argv[argc++] = r->key;
  }
  if (extra) {
    snprintf(r->extra_arg, sizeof r->extra_arg, "%s", extra);
    argv[argc++] = r->extra_arg;
  }
  argv[argc] = NULL;
  ck_assert_int_eq(args_parse(argc, argv, &r->cfg), ARGS_OK);
  signals_install();
  htdocs_template_init(&r->cfg);
  r->channels = channels_build(&r->cfg);
  ck_assert_ptr_nonnull(r->channels);
  dipixy_metrics_init(&r->mx, &r->cfg);
  ck_assert_int_eq(pthread_create(&r->th, NULL, run_main, r), 0);
  for (int i = 0; i < WAIT_MS / 5 && !atomic_load(&g_listening); i++) nanosleep(&ts, NULL);
  ck_assert_int_eq(atomic_load(&g_listening), 1);
}

static void run_stop(run_t *r) {
  char cmd[96];

  ck_assert_int_eq(raise(SIGTERM), 0);
  pthread_join(r->th, NULL);
  ck_assert_int_eq(r->rc, 0);
  dipixy_metrics_close(&r->mx);
  channels_free(r->channels);
  args_free(&r->cfg);
  if (r->dir[0]) {
    snprintf(cmd, sizeof cmd, "rm -rf %s", r->dir);
    ck_assert_int_eq(system(cmd), 0);
  }
}

static int connect_to(unsigned port) {
  struct sockaddr_in a = {0};
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(connect(fd, (struct sockaddr *)&a, sizeof a), 0);
  return fd;
}

static size_t read_reply(int fd, SSL *ssl, char *buf, size_t cap) {
  size_t total = 0;

  for (;;) {
    ssize_t n;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};

    if (!ssl && poll(&pfd, 1, WAIT_MS) <= 0) break;
    n = ssl ? SSL_read(ssl, buf + total, (int)(cap - 1 - total)) : read(fd, buf + total, cap - 1 - total);
    if (n <= 0) break;
    total += (size_t)n;
    if (total >= cap - 1) break;
  }
  buf[total] = '\0';
  return total;
}

static size_t plain_get(unsigned port, const char *path, char *buf, size_t cap) {
  char req[256];
  int fd = connect_to(port);
  size_t n;

  snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", path);
  ck_assert_int_eq((int)write(fd, req, strlen(req)), (int)strlen(req));
  n = read_reply(fd, NULL, buf, cap);
  close(fd);
  return n;
}

START_TEST(listening_callback_receives_the_live_configuration) {
  run_t r;

  run_start(&r, 0, NULL);
  ck_assert_ptr_eq(atomic_load(&g_listening_cfg), &r.cfg);
  ck_assert_int_eq(reactor_worker_count(), 1);
  run_stop(&r);
}
END_TEST

START_TEST(status_page_and_unknown_routes_are_served_over_plain_http) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NULL);
  ck_assert_uint_gt(plain_get(r.port, "/no/such/route", reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "404"));
  ck_assert_uint_gt(plain_get(r.port, "/", reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "dipixy test page"));
  ck_assert_int_ge((int)reactor_connections_total(), 2);
  run_stop(&r);
}
END_TEST

START_TEST(status_page_can_be_switched_off) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, "--no-status");
  ck_assert_uint_gt(plain_get(r.port, "/", reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "404"));
  run_stop(&r);
}
END_TEST

START_TEST(tls_listener_negotiates_and_serves_http11) {
  run_t r;
  char reply[REPLY_MAX];
  SSL_CTX *cctx;
  SSL *ssl;
  int fd;
  static const char req[] = "GET /no/such/route HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  const unsigned char *alpn = NULL;
  unsigned alpn_len = 0;

  run_start(&r, 1, NULL);
  ck_assert_int_eq(tls_is_running(), 1);
  cctx = SSL_CTX_new(TLS_client_method());
  ck_assert_ptr_nonnull(cctx);
  SSL_CTX_set_verify(cctx, SSL_VERIFY_NONE, NULL);
  fd = connect_to(r.tls_port);
  ssl = SSL_new(cctx);
  SSL_set_fd(ssl, fd);
  ck_assert_int_eq(SSL_set_alpn_protos(ssl, (const unsigned char *)"\x08http/1.1", 9), 0);
  ck_assert_int_eq(SSL_connect(ssl), 1);
  SSL_get0_alpn_selected(ssl, &alpn, &alpn_len);
  ck_assert_uint_eq(alpn_len, 8u);
  ck_assert_mem_eq(alpn, "http/1.1", 8);
  ck_assert_int_eq(SSL_write(ssl, req, (int)sizeof req - 1), (int)sizeof req - 1);
  ck_assert_uint_gt(read_reply(fd, ssl, reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "404"));
  SSL_free(ssl);
  close(fd);
  SSL_CTX_free(cctx);
  run_stop(&r);
}
END_TEST

START_TEST(websocket_upgrade_then_ping_pong_and_close_over_plain_http) {
  run_t r;
  char reply[REPLY_MAX];
  int fd;
  ssize_t n;
  uint8_t frame[64];
  size_t flen;
  static const uint8_t ping[] = {'h', 'i'};
  static const uint8_t close_code[] = {0x03, 0xE8};
  static const char upgrade[] =
      "GET /ui/ws/ HTTP/1.1\r\nHost: x\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";

  run_start(&r, 0, NULL);
  fd = connect_to(r.port);
  ck_assert_int_eq((int)write(fd, upgrade, sizeof upgrade - 1), (int)sizeof upgrade - 1);
  n = recv(fd, reply, sizeof reply - 1, 0);
  ck_assert_int_gt((int)n, 0);
  reply[n] = '\0';
  ck_assert_ptr_nonnull(strstr(reply, "101 Switching Protocols"));
  ck_assert_ptr_nonnull(strstr(reply, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));

  flen = client_ws_masked_frame(frame, 0x9, ping, sizeof ping);
  ck_assert_int_eq((int)write(fd, frame, flen), (int)flen);
  n = recv(fd, reply, sizeof reply, 0);
  ck_assert_int_eq((int)n, 4);
  ck_assert_uint_eq((uint8_t)reply[0], 0x8A);
  ck_assert_mem_eq(reply + 2, "hi", 2);

  flen = client_ws_masked_frame(frame, 0x8, close_code, sizeof close_code);
  ck_assert_int_eq((int)write(fd, frame, flen), (int)flen);
  n = recv(fd, reply, sizeof reply, 0);
  ck_assert_int_eq((int)n, 4);
  ck_assert_uint_eq((uint8_t)reply[0], 0x88);
  ck_assert_int_eq((int)recv(fd, reply, sizeof reply, 0), 0);
  close(fd);
  run_stop(&r);
}
END_TEST

START_TEST(websocket_upgrade_without_the_required_headers_is_rejected) {
  run_t r;
  char reply[REPLY_MAX];
  static const char bad[] = "GET /ui/ws/ HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  int fd;

  run_start(&r, 0, NULL);
  fd = connect_to(r.port);
  ck_assert_int_eq((int)write(fd, bad, sizeof bad - 1), (int)sizeof bad - 1);
  ck_assert_uint_gt(read_reply(fd, NULL, reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "400 Bad Request"));
  ck_assert_ptr_null(strstr(reply, "101"));
  close(fd);
  run_stop(&r);
}
END_TEST

static Suite *run_suite(void) {
  Suite *s = suite_create("dipixy_reactor_run");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, listening_callback_receives_the_live_configuration);
  tcase_add_test(tc, status_page_and_unknown_routes_are_served_over_plain_http);
  tcase_add_test(tc, status_page_can_be_switched_off);
  tcase_add_test(tc, tls_listener_negotiates_and_serves_http11);
  tcase_add_test(tc, websocket_upgrade_then_ping_pong_and_close_over_plain_http);
  tcase_add_test(tc, websocket_upgrade_without_the_required_headers_is_rejected);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(run_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
