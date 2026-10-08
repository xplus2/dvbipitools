/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdatomic.h>
#include <string.h>

#include "client_resp.h"
#include "reactor_rig.h"

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
  static const char *const no_status[] = {"--no-status", NULL};
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, no_status);
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
