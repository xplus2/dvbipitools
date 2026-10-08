/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_REACTOR_RIG_H
#define DIPIXY_TEST_REACTOR_RIG_H

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
#include "dipixy/core/htdocs.h"
#include "dipixy/core/metrics.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/reactor.h"
#include "dipixy/ts/channels/channels.h"
#include "lib/sys/signal.h"

#define REPLY_MAX 4096
#define WAIT_MS 5000
#define ARGV_MAX 24

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
  unsigned port;
  unsigned tls_port;
} run_t;

static _Atomic int g_listening;
static _Atomic(const config_t *) g_listening_cfg;

static inline void on_listening(const config_t *cfg) {
  atomic_store(&g_listening_cfg, cfg);
  atomic_store(&g_listening, 1);
}

static inline unsigned free_tcp_port(void) {
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

static inline void *run_main(void *arg) {
  run_t *r = arg;

  r->rc = reactor_run(&r->cfg, r->channels, &r->mx, on_listening);
  return NULL;
}

static inline void run_start(run_t *r, int with_tls, const char *const *extra) {
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
  for (size_t i = 0; extra && extra[i]; i++) {
    ck_assert_int_lt(argc, ARGV_MAX - 1);
    argv[argc++] = (char *)extra[i];
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

static inline void run_stop(run_t *r) {
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

static inline int connect_to(unsigned port) {
  struct sockaddr_in a = {0};
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(connect(fd, (struct sockaddr *)&a, sizeof a), 0);
  return fd;
}

static inline size_t read_reply(int fd, SSL *ssl, char *buf, size_t cap) {
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

static inline size_t plain_get(unsigned port, const char *path, char *buf, size_t cap) {
  char req[256];
  int fd = connect_to(port);
  size_t n;

  snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", path);
  ck_assert_int_eq((int)write(fd, req, strlen(req)), (int)strlen(req));
  n = read_reply(fd, NULL, buf, cap);
  close(fd);
  return n;
}

static inline size_t raw_exchange(unsigned port, const char *req, char *buf, size_t cap) {
  int fd = connect_to(port);
  size_t n;

  ck_assert_int_eq((int)write(fd, req, strlen(req)), (int)strlen(req));
  n = read_reply(fd, NULL, buf, cap);
  close(fd);
  return n;
}

#endif
