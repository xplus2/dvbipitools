/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/dlna/gena.h"

#define PEER "127.0.0.1"

static config_t g_cfg;

static int listen_local(unsigned *port) {
  struct sockaddr_in sa;
  socklen_t len = sizeof sa;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&sa, sizeof sa), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&sa, &len), 0);
  *port = ntohs(sa.sin_port);
  return fd;
}

/* accepts one NOTIFY, answers 200, copies request into out */
static void take_notify(int lfd, char *out, size_t outsz) {
  struct pollfd pfd = {lfd, POLLIN, 0};
  size_t n = 0;
  int fd;
  ck_assert_int_gt(poll(&pfd, 1, 5000), 0);
  fd = accept(lfd, NULL, NULL);
  ck_assert_int_ge(fd, 0);
  while (n + 1 < outsz && !memmem(out, n, "</e:propertyset>", 16)) {
    struct pollfd cp = {fd, POLLIN, 0};
    ssize_t r;
    ck_assert_int_gt(poll(&cp, 1, 5000), 0);
    r = read(fd, out + n, outsz - 1 - n);
    ck_assert_int_gt(r, 0);
    n += (size_t)r;
  }
  out[n] = '\0';
  ck_assert_int_eq((int)write(fd, "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", 38), 38);
  close(fd);
}

static void callback_for(unsigned port, char *out, size_t outsz) {
  snprintf(out, outsz, "<http://127.0.0.1:%u/cb>", port);
}

START_TEST(subscribe_returns_sid_and_timeout) {
  char cb[64], sid[64];
  unsigned port, t = 0;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", "Second-600", sid, sizeof sid, &t), 200);
  ck_assert_int_eq(strncmp(sid, "uuid:", 5), 0);
  ck_assert_uint_eq(t, 600);
  close(lfd);
}
END_TEST

START_TEST(initial_notify_is_sent_with_seq_zero) {
  char cb[64];
  char sid[64];
  char req[2048];
  unsigned port;
  unsigned t;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_uint_eq(t, 1800);
  take_notify(lfd, req, sizeof req);
  ck_assert_ptr_nonnull(strstr(req, "NOTIFY /cb HTTP/1.1"));
  ck_assert_ptr_nonnull(strstr(req, "SEQ: 0"));
  ck_assert_ptr_nonnull(strstr(req, sid));
  ck_assert_ptr_nonnull(strstr(req, "<SystemUpdateID>"));
  close(lfd);
}
END_TEST

START_TEST(cm_initial_notify_carries_protocol_info) {
  char cb[64];
  char sid[64];
  char req[2048];
  unsigned port;
  unsigned t;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CM, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  take_notify(lfd, req, sizeof req);
  ck_assert_ptr_nonnull(strstr(req, "<SourceProtocolInfo>http-get:"));
  ck_assert_ptr_nonnull(strstr(req, "<CurrentConnectionIDs>0<"));
  close(lfd);
}
END_TEST

START_TEST(update_bumps_id_and_notifies_subscriber) {
  char cb[64];
  char sid[64];
  char req[2048];
  char want[48];
  unsigned port;
  unsigned t;
  unsigned before;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  take_notify(lfd, req, sizeof req);
  before = gena_system_update_id();
  gena_notify_system_update();
  ck_assert_uint_eq(gena_system_update_id(), before + 1);
  take_notify(lfd, req, sizeof req);
  ck_assert_ptr_nonnull(strstr(req, "SEQ: 1"));
  snprintf(want, sizeof want, "<SystemUpdateID>%u<", before + 1);
  ck_assert_ptr_nonnull(strstr(req, want));
  close(lfd);
}
END_TEST

START_TEST(update_id_is_nonzero) {
  ck_assert_uint_ne(gena_system_update_id(), 0);
}
END_TEST

START_TEST(renew_unknown_sid_is_412) {
  char sid[64];
  unsigned t;
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, "uuid:nope", NULL, NULL, NULL, sid, sizeof sid, &t), 412);
}
END_TEST

START_TEST(renew_known_sid_keeps_it) {
  char cb[64];
  char sid[64];
  char sid2[64];
  unsigned port;
  unsigned t;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, sid, NULL, NULL, "Second-300", sid2, sizeof sid2, &t), 200);
  ck_assert_str_eq(sid, sid2);
  ck_assert_uint_eq(t, 300);
  close(lfd);
}
END_TEST

START_TEST(renew_with_callback_is_400) {
  char sid[64];
  unsigned t;
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, "uuid:x", "<http://127.0.0.1:1/>", NULL, NULL, sid, sizeof sid, &t), 400);
}
END_TEST

START_TEST(subscribe_bad_input_is_412) {
  char sid[64];
  unsigned t;
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, NULL, "upnp:event", NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "<http://127.0.0.1:1/>", NULL, NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "<http://127.0.0.1:1/>", "upnp:other", NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "<http://example.invalid/>", "upnp:event", NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "http://127.0.0.1:1/", "upnp:event", NULL, sid, sizeof sid, &t), 412);
}
END_TEST

START_TEST(sid_is_bound_to_service) {
  char cb[64];
  char sid[64];
  char sid2[64];
  unsigned port;
  unsigned t;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CM, PEER, sid, NULL, NULL, NULL, sid2, sizeof sid2, &t), 412);
  close(lfd);
}
END_TEST

START_TEST(unsubscribe_removes_then_412) {
  char cb[64];
  char sid[64];
  unsigned port;
  unsigned t;
  int lfd = listen_local(&port);
  callback_for(port, cb, sizeof cb);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, sid, NULL, NULL), 200);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, sid, NULL, NULL), 412);
  close(lfd);
}
END_TEST

START_TEST(unsubscribe_header_rules) {
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, NULL, NULL, NULL), 412);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, "uuid:x", NULL, "upnp:event"), 400);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, "uuid:x", "<http://127.0.0.1:1/>", NULL), 400);
}
END_TEST

START_TEST(callback_must_match_peer) {
  char sid[64];
  unsigned t;
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "192.0.2.7", NULL, "<http://127.0.0.1:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "192.0.2.7", NULL, "<http://[::1]:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 412);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "192.0.2.7", NULL, "<http://192.0.2.7:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "::1", NULL, "<http://[::1]:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "", NULL, "<http://192.0.2.7:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 412);
}
END_TEST

START_TEST(sid_is_bound_to_peer) {
  char sid[64];
  char sid2[64];
  unsigned t;
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "<http://127.0.0.1:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 200);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "192.0.2.7", sid, NULL, NULL, NULL, sid2, sizeof sid2, &t), 412);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, "192.0.2.7", sid, NULL, NULL), 412);
  ck_assert_int_eq(gena_unsubscribe(GENA_SVC_CD, PEER, sid, NULL, NULL), 200);
}
END_TEST

START_TEST(per_peer_limit_leaves_room_for_others) {
  char sid[64];
  unsigned t;
  int n = 0;
  int status = 200;
  while (status == 200 && n < 64) {
    status = gena_subscribe(&g_cfg, GENA_SVC_CD, PEER, NULL, "<http://127.0.0.1:1/>", "upnp:event", NULL, sid, sizeof sid, &t);
    if (status == 200) n++;
  }
  ck_assert_int_eq(status, 503);
  ck_assert_int_lt(n, 32);
  ck_assert_int_eq(gena_subscribe(&g_cfg, GENA_SVC_CD, "192.0.2.7", NULL, "<http://192.0.2.7:1/>", "upnp:event", NULL, sid, sizeof sid, &t), 200);
}
END_TEST

START_TEST(table_full_is_503) {
  char sid[64];
  char peer[24];
  char cb[48];
  unsigned t;
  int status = 200;
  int n = 0;
  while (status == 200 && n < 200) {
    snprintf(peer, sizeof peer, "192.0.2.%d", n % 250 + 1);
    snprintf(cb, sizeof cb, "<http://%s:1/>", peer);
    status = gena_subscribe(&g_cfg, GENA_SVC_CD, peer, NULL, cb, "upnp:event", NULL, sid, sizeof sid, &t);
    n++;
  }
  ck_assert_int_eq(status, 503);
}
END_TEST

static Suite *gena_suite(void) {
  Suite *s = suite_create("dipixy_gena");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, NULL, gena_stop);
  tcase_add_test(tc, subscribe_returns_sid_and_timeout);
  tcase_add_test(tc, initial_notify_is_sent_with_seq_zero);
  tcase_add_test(tc, cm_initial_notify_carries_protocol_info);
  tcase_add_test(tc, update_bumps_id_and_notifies_subscriber);
  tcase_add_test(tc, update_id_is_nonzero);
  tcase_add_test(tc, renew_unknown_sid_is_412);
  tcase_add_test(tc, renew_known_sid_keeps_it);
  tcase_add_test(tc, renew_with_callback_is_400);
  tcase_add_test(tc, subscribe_bad_input_is_412);
  tcase_add_test(tc, sid_is_bound_to_service);
  tcase_add_test(tc, unsubscribe_removes_then_412);
  tcase_add_test(tc, unsubscribe_header_rules);
  tcase_add_test(tc, callback_must_match_peer);
  tcase_add_test(tc, sid_is_bound_to_peer);
  tcase_add_test(tc, per_peer_limit_leaves_room_for_others);
  tcase_add_test(tc, table_full_is_503);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(gena_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
