/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "cas_fixture.h"

#include <poll.h>

#include "dipidescramble/emmcache.h"
#include "dipidescramble/ipiclient.h"

static ipiclient_poll_state_t drive(ipiclient_poll_t *p, int max_iters) {
  ipiclient_poll_state_t st = IPICLIENT_POLL_PENDING;
  for (int i = 0; i < max_iters && st == IPICLIENT_POLL_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = ipiclient_poll_fd(p);
    pfd.events = ipiclient_poll_events(p);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = ipiclient_poll_step(p);
  }
  return st;
}

static size_t put_unit(unsigned char *out, const unsigned char *emm, size_t emm_len) {
  out[0] = 0;
  out[1] = (unsigned char)(emm_len >> 8);
  out[2] = (unsigned char)emm_len;
  memcpy(out + 3, emm, emm_len);
  return 3 + emm_len;
}

START_TEST(ipiclient_poll_completes_for_body_and_feeds_units) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  unsigned char body[2048];
  unsigned char emm[1024];
  unsigned char bk[CRYPTO_KEY_LEN];
  unsigned char sk[CRYPTO_KEY_LEN];
  unsigned char ecm[64];
  unsigned char cw[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  unsigned char cw_out[16];
  unsigned char saved[2048];
  char resp[4096];
  size_t blen = 0;
  size_t emm_total = 0;
  size_t n;
  int rl;
  int tfd;
  char uri[64];
  char path[] = "/tmp/dipidescramble_test_ipiclient_cache_XXXXXX";
  FILE *f;
  ipiclient_t *c;
  ipiclient_poll_t *p;
  EVP_PKEY *key = make_rsa_key();
  device_state_t *d = make_device_for_key(key, TEST_SERIAL, 0);
  emmcache_t *cache = emmcache_new(0);

  memset(bk, 0x11, sizeof bk);
  memset(sk, 0x22, sizeof sk);
  n = build_emm_u(key, bk, emm, sizeof emm);
  blen += put_unit(body + blen, emm, n);
  emm_total += n;
  n = build_emm_g(bk, sk, 0x0064, emm, sizeof emm);
  blen += put_unit(body + blen, emm, n);
  emm_total += n;

  rl = snprintf(resp, sizeof resp, "HTTP/1.1 200 OK\r\nETag: \"v1\"\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", blen);
  memcpy(resp + rl, body, blen);
  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = (size_t)rl + blen;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/emm", port);
  c = ipiclient_new(uri, 0, NULL);
  ck_assert_ptr_nonnull(c);
  p = ipiclient_poll_start(c);
  ck_assert_ptr_nonnull(p);
  ck_assert_int_eq(drive(p, 200), IPICLIENT_POLL_DONE);
  ck_assert_int_eq(ipiclient_poll_take(p, cache, d), 1);

  n = build_ecm(sk, cw, 8, ecm, sizeof ecm);
  ck_assert_int_eq(device_resolve_cw(d, ecm, n, 0x0064, 8, TEST_ECM_PID, cw_out), 0);
  ck_assert_mem_eq(cw_out, cw, 8);

  tfd = mkstemp(path);
  ck_assert_int_ge(tfd, 0);
  close(tfd);
  ck_assert_int_eq(emmcache_save(cache, path), 0);
  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fread(saved, 1, sizeof saved, f), emm_total);
  fclose(f);
  unlink(path);

  pthread_join(th, NULL);
  close(listen_fd);
  ipiclient_free(c);
  emmcache_free(cache);
  device_state_free(d);
  EVP_PKEY_free(key);
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
  EVP_PKEY *key;
  device_state_t *d = make_device(&key);
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
  EVP_PKEY_free(key);
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
