/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/reactor/reactor_tls.h"

static int tcp_pair(int fds[2]) {
  struct sockaddr_in sa;
  socklen_t sl = sizeof sa;
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  if (ls < 0) return -1;
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(ls, (struct sockaddr *)&sa, sizeof sa) || listen(ls, 1) || getsockname(ls, (struct sockaddr *)&sa, &sl)) {
    close(ls);
    return -1;
  }
  fds[0] = socket(AF_INET, SOCK_STREAM, 0);
  if (fds[0] < 0 || connect(fds[0], (struct sockaddr *)&sa, sizeof sa)) {
    close(ls);
    return -1;
  }
  fds[1] = accept(ls, NULL, NULL);
  close(ls);
  return fds[1] < 0 ? -1 : 0;
}

struct zc_acc {
  unsigned calls;
  uint32_t lo;
  uint32_t hi;
};

static void zc_cb(uint32_t lo, uint32_t hi, void *arg) {
  struct zc_acc *a = arg;
  a->calls++;
  a->lo = lo;
  a->hi = hi;
}

START_TEST(lifecycle_calls_are_inert) {
  tls_set_http2_enabled(0);
  ck_assert_int_eq(tls_init("/no/cert.pem", "/no/key.pem", 16), 0);
  tls_gc_init(4);
  tls_gc_sweep();
  tls_ctx_gc_sweep();
  tls_cleanup();
  ck_assert_int_eq(tls_is_running(), 0);
  ck_assert_int_eq(reload_tls(), -1);
}
END_TEST

START_TEST(handshake_entry_points_fail) {
  ck_assert_int_eq(tls_accept(3), -1);
  ck_assert_int_eq(tls_handshake(3), -1);
}
END_TEST

START_TEST(connection_queries_report_plain) {
  char cn[8] = "xxxxxxx";
  ck_assert_int_eq(tls_is_tls(3), 0);
  ck_assert_int_eq(tls_client_cert_verified(3), 0);
  ck_assert_int_eq(tls_has_pending(3), 0);
  ck_assert_int_eq(tls_alpn_is_h2(3), 0);
  tls_get_client_cert_cn(3, cn, sizeof cn);
  ck_assert_str_eq(cn, "");
  tls_get_client_cert_cn(3, cn, 0);
}
END_TEST

START_TEST(cert_info_is_empty) {
  char buf[16] = "xxxxxxxxxxxxxxx";
  tls_cert_detail_t d;
  memset(&d, 0x5A, sizeof d);
  tls_cert_info(buf, sizeof buf, "/no/cert.pem", 1);
  ck_assert_str_eq(buf, "");
  tls_cert_info(buf, 0, "/no/cert.pem", 0);
  ck_assert_int_eq(tls_cert_detail("/no/cert.pem", 1, &d), 0);
  ck_assert_int_eq(d.alias_count, 0);
  ck_assert_str_eq(d.cn, "");
}
END_TEST

START_TEST(plain_send_recv_and_close) {
  int sv[2];
  char out[8];
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  ck_assert_int_eq(tls_net_send(sv[0], "hello", 5), 5);
  ck_assert_int_eq(tls_net_recv(sv[1], out, sizeof out), 5);
  ck_assert_mem_eq(out, "hello", 5);
  tls_close_fd(sv[0]);
  tls_close_fd(-1);
  ck_assert_int_eq(tls_net_recv(sv[1], out, sizeof out), 0);
  close(sv[1]);
}
END_TEST

START_TEST(zerocopy_send_delivers_and_completion_drains) {
  int fds[2];
  int one = 1;
  int used = -1;
  char out[64];
  struct zc_acc acc;
  struct pollfd p;
  ssize_t r;
  memset(&acc, 0, sizeof acc);
  ck_assert_int_eq(tcp_pair(fds), 0);
  ck_assert_int_eq(setsockopt(fds[0], SOL_SOCKET, SO_ZEROCOPY, &one, sizeof one), 0);

  r = tls_net_send_zc(fds[0], "zerocopy-payload", 16, &used);
  ck_assert_int_eq(r, 16);
  ck_assert(used == 0 || used == 1);
  ck_assert_int_eq(tls_net_recv(fds[1], out, sizeof out), 16);
  ck_assert_mem_eq(out, "zerocopy-payload", 16);

  if (used) {
    p.fd = fds[0];
    p.events = 0;
    poll(&p, 1, 2000);
    ck_assert_int_eq(tls_zc_drain(fds[0], zc_cb, &acc), 1);
    ck_assert_uint_ge(acc.calls, 1);
    ck_assert_uint_ge(acc.hi, acc.lo);
  }
  ck_assert_int_eq(tls_zc_drain(fds[0], zc_cb, &acc), 0);
  close(fds[0]);
  close(fds[1]);
}
END_TEST

START_TEST(zerocopy_send_error_leaves_used_zc_clear) {
  int used = -1;
  ck_assert_int_lt(tls_net_send_zc(-1, "x", 1, &used), 0);
  ck_assert_int_eq(used, 0);
}
END_TEST

START_TEST(drain_without_errqueue_reports_none) {
  int sv[2];
  struct zc_acc acc;
  memset(&acc, 0, sizeof acc);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  ck_assert_int_eq(tls_zc_drain(sv[0], zc_cb, &acc), 0);
  ck_assert_uint_eq(acc.calls, 0);
  close(sv[0]);
  close(sv[1]);
}
END_TEST

static Suite *tls_stub_suite(void) {
  Suite *s = suite_create("dipixy_tls_stub");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, lifecycle_calls_are_inert);
  tcase_add_test(tc, handshake_entry_points_fail);
  tcase_add_test(tc, connection_queries_report_plain);
  tcase_add_test(tc, cert_info_is_empty);
  tcase_add_test(tc, plain_send_recv_and_close);
  tcase_add_test(tc, zerocopy_send_delivers_and_completion_drains);
  tcase_add_test(tc, zerocopy_send_error_leaves_used_zc_clear);
  tcase_add_test(tc, drain_without_errqueue_reports_none);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
