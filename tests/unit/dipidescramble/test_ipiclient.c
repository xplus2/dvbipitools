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
#include <sys/time.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "dipidescramble/device.h"
#include "dipidescramble/emmcache.h"
#include "dipidescramble/ipiclient.h"

static char g_key_path[] = "/tmp/dipidescramble_test_ipiclient_key_XXXXXX";

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0)
    return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0)
      break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
      break;
  }
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

static ipiclient_poll_state_t drive(ipiclient_poll_t *p, int max_iters) {
  ipiclient_poll_state_t st = IPICLIENT_POLL_PENDING;
  int i;

  for (i = 0; i < max_iters && st == IPICLIENT_POLL_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = ipiclient_poll_fd(p);
    pfd.events = ipiclient_poll_events(p);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = ipiclient_poll_step(p);
  }
  return st;
}

static device_state_t *make_device(void) {
  EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
  EVP_PKEY *pkey = NULL;
  ecm_profile_t no_profile;
  int fd;
  FILE *f;
  device_state_t *d;

  memset(&no_profile, 0, sizeof no_profile);
  ck_assert_int_gt(EVP_PKEY_keygen_init(ctx), 0);
  ck_assert_int_gt(EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048), 0);
  ck_assert_int_gt(EVP_PKEY_keygen(ctx, &pkey), 0);
  EVP_PKEY_CTX_free(ctx);

  strcpy(g_key_path, "/tmp/dipidescramble_test_ipiclient_key_XXXXXX");
  fd = mkstemp(g_key_path);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_int_eq(PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL), 1);
  fclose(f);

  d = device_state_new(g_key_path, "test-serial", &no_profile, 0);
  ck_assert_ptr_nonnull(d);
  remove(g_key_path);
  EVP_PKEY_free(pkey);
  return d;
}

START_TEST(ipiclient_poll_completes_for_body_and_feeds_units) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  unsigned char unit[8] = {0, 0x00, 0x05, 'x', 'x', 'x', 'x', 'x'};
  char resp[256];
  int rl;
  char uri[64];
  ipiclient_t *c;
  ipiclient_poll_t *p;
  device_state_t *d = make_device();
  emmcache_t *cache = emmcache_new(0);

  rl = snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nETag: \"v1\"\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", (size_t)6);
  memcpy(resp + rl, unit, 6);
  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = (size_t)rl + 6;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/emm", port);
  c = ipiclient_new(uri, 0, NULL);
  ck_assert_ptr_nonnull(c);
  p = ipiclient_poll_start(c);
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(drive(p, 200), IPICLIENT_POLL_DONE);
  ipiclient_poll_take(p, cache, d);

  pthread_join(th, NULL);
  close(listen_fd);
  ipiclient_free(c);
  emmcache_free(cache);
  device_state_free(d);
}
END_TEST

START_TEST(ipiclient_poll_completes_for_not_modified) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n";
  char uri[64];
  ipiclient_t *c;
  ipiclient_poll_t *p;
  device_state_t *d = make_device();
  emmcache_t *cache = emmcache_new(0);

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/emm", port);
  c = ipiclient_new(uri, 0, NULL);
  ck_assert_ptr_nonnull(c);
  p = ipiclient_poll_start(c);
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(drive(p, 200), IPICLIENT_POLL_DONE);
  ck_assert_int_eq(ipiclient_poll_take(p, cache, d), 0);

  pthread_join(th, NULL);
  close(listen_fd);
  ipiclient_free(c);
  emmcache_free(cache);
  device_state_free(d);
}
END_TEST

START_TEST(ipiclient_poll_reports_error_on_refused_connection) {
  ipiclient_t *c = ipiclient_new("http://127.0.0.1:1/nothing", 0, NULL);
  ipiclient_poll_t *p;

  ck_assert_ptr_nonnull(c);
  p = ipiclient_poll_start(c);
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(drive(p, 200), IPICLIENT_POLL_ERROR);
  ipiclient_poll_free(p);
  ipiclient_free(c);
}
END_TEST

static Suite *ipiclient_suite(void) {
  Suite *s = suite_create("ipiclient");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ipiclient_poll_completes_for_body_and_feeds_units);
  tcase_add_test(tc, ipiclient_poll_completes_for_not_modified);
  tcase_add_test(tc, ipiclient_poll_reports_error_on_refused_connection);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ipiclient_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
