/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdatomic.h>
#include <string.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "dipixy/core/status.h"

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

static int ws_open(unsigned port) {
  static const char upgrade[] =
      "GET /ui/ws/ HTTP/1.1\r\nHost: x\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
      "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
  char reply[REPLY_MAX];
  int fd = connect_to(port);
  ssize_t n;

  ck_assert_int_eq((int)write(fd, upgrade, sizeof upgrade - 1), (int)sizeof upgrade - 1);
  n = recv(fd, reply, sizeof reply - 1, 0);
  ck_assert_int_gt((int)n, 0);
  reply[n] = '\0';
  ck_assert_ptr_nonnull(strstr(reply, "101 Switching Protocols"));
  return fd;
}

/* text reply of the next data frame, skipping broadcast ticks. 0 if the peer closed */
static size_t ws_text_reply(int fd, const char *want, char *out, size_t cap) {
  for (int tries = 0; tries < 20; tries++) {
    uint8_t hdr[2];
    size_t plen;
    size_t got = 0;
    struct pollfd pfd = {.fd = fd, .events = POLLIN};

    if (poll(&pfd, 1, WAIT_MS) <= 0) return 0;
    if (recv(fd, hdr, 2, MSG_WAITALL) != 2) return 0;
    plen = hdr[1] & 0x7F;
    if (plen == 126) {
      uint8_t ext[2];

      if (recv(fd, ext, 2, MSG_WAITALL) != 2) return 0;
      plen = (size_t)(ext[0] << 8 | ext[1]);
    }
    if (plen >= cap) return 0;
    while (got < plen) {
      ssize_t n = recv(fd, out + got, plen - got, 0);

      if (n <= 0) return 0;
      got += (size_t)n;
    }
    out[plen] = '\0';
    if (strstr(out, want)) return plen;
  }
  return 0;
}

static void ws_send_text(int fd, const char *text) {
  uint8_t frame[512];
  size_t flen = client_ws_masked_frame(frame, 0x1, (const uint8_t *)text, strlen(text));

  ck_assert_int_eq((int)write(fd, frame, flen), (int)flen);
}

START_TEST(websocket_text_commands_are_answered_and_reloads_are_accepted) {
  run_t r;
  char out[16384];
  int fd;

  run_start(&r, 1, NULL);
  fd = ws_open(r.port);
  ws_send_text(fd, "{\"type\":\"clients.get\"}");
  ck_assert_uint_gt(ws_text_reply(fd, "clients.snapshot", out, sizeof out), 0u);
  ws_send_text(fd, "{\"type\":\"sources.get\"}");
  ck_assert_uint_gt(ws_text_reply(fd, "\"type\":\"sources", out, sizeof out), 0u);
  ws_send_text(fd, "{\"type\":\"tls.reload\"}");
  ck_assert_uint_gt(ws_text_reply(fd, "\"type\":\"tls.reload\"", out, sizeof out), 0u);
  ck_assert_ptr_nonnull(strstr(out, "\"ok\":true"));
  ws_send_text(fd, "{\"type\":\"playlists.reload\"}");
  ws_send_text(fd, "{\"type\":\"unknown.thing\"}");
  ws_send_text(fd, "{\"type\":\"clients.get\"}");
  ck_assert_uint_gt(ws_text_reply(fd, "clients.snapshot", out, sizeof out), 0u);
  close(fd);
  run_stop(&r);
}
END_TEST

static void drain(int fd) {
  char buf[64];
  ssize_t n;

  do {
    n = recv(fd, buf, sizeof buf, 0);
  } while (n > 0);
}

START_TEST(websocket_protocol_violations_and_hangups_close_the_connection) {
  static const uint8_t unmasked_text[] = {0x81, 0x02, 'h', 'i'};
  static const uint8_t reserved_opcode[] = {0x83, 0x80, 0x00, 0x00, 0x00, 0x00};
  run_t r;
  int fd;

  run_start(&r, 0, NULL);

  fd = ws_open(r.port);
  ck_assert_int_eq((int)write(fd, unmasked_text, sizeof unmasked_text), (int)sizeof unmasked_text);
  drain(fd);
  close(fd);

  fd = ws_open(r.port);
  ck_assert_int_eq((int)write(fd, reserved_opcode, sizeof reserved_opcode), (int)sizeof reserved_opcode);
  drain(fd);
  close(fd);

  fd = ws_open(r.port);
  close(fd);

  fd = ws_open(r.port);
  ck_assert_int_ge(fd, 0);
  close(fd);
  run_stop(&r);
}
END_TEST

START_TEST(status_script_reports_exec_args_and_the_tls_certificate) {
  static char *fake_argv[] = {"dipixy", "--example", NULL};
  run_t r;
  char reply[REPLY_MAX * 2];

  dipixy_status_init(2, fake_argv);
  run_start(&r, 1, NULL);
  ck_assert_uint_gt(plain_get(r.port, "/ui/status.js", reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "\"exec_args\":[\"dipixy\",\"--example\"]"));
  ck_assert_ptr_nonnull(strstr(reply, "\"tls\":{\"enabled\":true"));
  ck_assert_ptr_nonnull(strstr(reply, "\"cn\":"));
  ck_assert_ptr_nonnull(strstr(reply, "\"expiry\":"));
  run_stop(&r);
}
END_TEST

static int connect6(unsigned port) {
  struct sockaddr_in6 a = {0};
  int fd = socket(AF_INET6, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  a.sin6_family = AF_INET6;
  a.sin6_port = htons((uint16_t)port);
  a.sin6_addr = in6addr_loopback;
  ck_assert_int_eq(connect(fd, (struct sockaddr *)&a, sizeof a), 0);
  return fd;
}

static void expect_404_over(int fd) {
  static const char req[] = "GET /no/such/route HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  char reply[REPLY_MAX];

  ck_assert_int_eq((int)write(fd, req, sizeof req - 1), (int)sizeof req - 1);
  ck_assert_uint_gt(read_reply(fd, NULL, reply, sizeof reply), 0u);
  ck_assert_ptr_nonnull(strstr(reply, "404"));
  close(fd);
}

START_TEST(ipv6_specific_listener_serves_requests) {
  unsigned port = free_tcp_port();
  char spec[32];
  const char *args[] = {"-l", spec, NULL};
  run_t r;

  snprintf(spec, sizeof spec, "[::1]:%u", port);
  run_start(&r, 0, args);
  expect_404_over(connect6(port));
  run_stop(&r);
}
END_TEST

START_TEST(dual_stack_listener_serves_both_families) {
  unsigned port = free_tcp_port_dual();
  char spec[32];
  const char *args[] = {"-l", spec, NULL};
  run_t r;

  snprintf(spec, sizeof spec, "all:%u", port);
  run_start(&r, 0, args);
  expect_404_over(connect6(port));
  expect_404_over(connect_to(port));
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
  tcase_add_test(tc, websocket_text_commands_are_answered_and_reloads_are_accepted);
  tcase_add_test(tc, websocket_protocol_violations_and_hangups_close_the_connection);
  tcase_add_test(tc, status_script_reports_exec_args_and_the_tls_certificate);
  tcase_add_test(tc, ipv6_specific_listener_serves_requests);
  tcase_add_test(tc, dual_stack_listener_serves_both_families);
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
