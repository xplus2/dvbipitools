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
#include <unistd.h>

#include "lib/config/yamlcfg.h"
#include "lib/sys/signal.h"

#include "dipifccret/run/run.h"

#define ARGV_MAX 16
#define RING_BYTES 8192u

typedef struct {
  const char *name;
  const char *extra[4];
  int metrics;
} serve_case_t;

static const serve_case_t serve_cases[] = {
  {"defaults", {NULL}, 0},
  {"no ret", {"--no-ret", NULL}, 0},
  {"no fcc", {"--no-fcc", NULL}, 0},
  {"no mc ret", {"--no-mc-ret", NULL}, 0},
  {"no rsi", {"--no-rsi", NULL}, 0},
  {"rsi rides mc ret", {"--rsi-mc-ret", NULL}, 0},
  {"rsi hostname", {"--rsi-hostname", "example.org", NULL}, 0},
  {"resolve by port", {"--fcc-resolve-by-port", NULL}, 0},
  {"metrics exporter", {NULL}, 1},
};

static capture_t *open_ring_capture(const config_t *cfg, char *errbuf, size_t errbuf_len) {
  static unsigned char ring[RING_BYTES];

  (void)cfg;
  (void)errbuf;
  (void)errbuf_len;
  return capture_from_ring(ring, RING_BYTES, 1, NULL, 0);
}

static capture_t *open_failing_capture(const config_t *cfg, char *errbuf, size_t errbuf_len) {
  (void)cfg;
  snprintf(errbuf, errbuf_len, "capture unavailable");
  return NULL;
}

static unsigned free_udp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static int serve_with(char *const *extra, size_t n_extra, unsigned port, fccret_open_capture_fn open_capture) {
  char listen_arg[48];
  char *argv[ARGV_MAX] = {"dipifccret", "-g", "239.0.0.0/8", "-l", listen_arg, "-I", "lo", "-M", "4", "-w", "1"};
  size_t argc = 11;
  config_t cfg;
  metrics_exporter_t mx;
  int rc;

  snprintf(listen_arg, sizeof listen_arg, "127.0.0.1:%u", port);
  for (size_t i = 0; i < n_extra; i++) argv[argc++] = extra[i];
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse((int)argc, argv, &cfg), ARGS_OK);
  signals_install();
  raise(SIGTERM);
  metrics_exporter_init(&mx, METRICS_COMPONENT_FCCRET, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  rc = fccret_serve(&cfg, &mx, open_capture);
  yamlcfg_strpool_free(cfg.str_pool);
  return rc;
}

START_TEST(serve_starts_and_stops_cleanly_for_each_configuration) {
  const serve_case_t *c = &serve_cases[_i];
  char *extra[8];
  size_t n = 0;
  char sock[96];
  int rc;

  for (size_t i = 0; c->extra[i]; i++) extra[n++] = (char *)c->extra[i];
  if (c->metrics) {
    snprintf(sock, sizeof sock, "/tmp/dvbipitools_serve_%d.sock", (int)getpid());
    extra[n++] = "--metrics-id";
    extra[n++] = "serve-test";
    extra[n++] = "--metrics";
    extra[n++] = sock;
  }
  rc = serve_with(extra, n, free_udp_port(), open_ring_capture);
  if (c->metrics) unlink(sock);
  ck_assert_msg(rc == 0, "%s: rc %d", c->name, rc);
}
END_TEST

START_TEST(serve_fails_when_the_capture_cannot_be_opened) {
  ck_assert_int_eq(serve_with(NULL, 0, free_udp_port(), open_failing_capture), 1);
}
END_TEST

START_TEST(serve_fails_when_privileges_cannot_be_dropped) {
  char *extra[] = {"-u", "no-such-user-dvbipitools-test"};

  ck_assert_int_eq(serve_with(extra, 2, free_udp_port(), open_ring_capture), 1);
}
END_TEST

START_TEST(serve_fails_when_the_listen_port_is_taken) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int blocker = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(blocker, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(blocker, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(blocker, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  ck_assert_int_eq(serve_with(NULL, 0, port, open_ring_capture), 1);
  close(blocker);
}
END_TEST

static Suite *serve_suite(void) {
  Suite *s = suite_create("dipifccret_serve");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 30);
  tcase_add_loop_test(tc, serve_starts_and_stops_cleanly_for_each_configuration, 0, (int)(sizeof serve_cases / sizeof serve_cases[0]));
  tcase_add_test(tc, serve_fails_when_the_capture_cannot_be_opened);
  tcase_add_test(tc, serve_fails_when_privileges_cannot_be_dropped);
  tcase_add_test(tc, serve_fails_when_the_listen_port_is_taken);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(serve_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
