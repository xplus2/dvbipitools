/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <librist/librist.h>

#include "lib/net/rist/ristin.h"

#define TEST_PORT 15982 /* librist rejects odd ports */

START_TEST(rejects_non_rist_scheme) {
  ristin_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peer_uri = "udp://@127.0.0.1:15982";
  ck_assert_ptr_null(ristin_open(&cfg));
}
END_TEST

START_TEST(rejects_missing_at) {
  ristin_cfg_t cfg;

  memset(&cfg, 0, sizeof cfg);
  cfg.peer_uri = "rist://127.0.0.1:15982"; /* dial, not listen: wrong role for input */
  ck_assert_ptr_null(ristin_open(&cfg));
}
END_TEST

/* blocking pipe read end: bound wait so a delivery failure fails test instead of hanging */
static ssize_t read_with_timeout(int fd, void *buf, size_t cap, int timeout_ms) {
  struct pollfd pfd = {.fd = fd, .events = POLLIN};

  if (poll(&pfd, 1, timeout_ms) <= 0)
    return -1;
  return read(fd, buf, cap);
}

START_TEST(receives_payload_from_a_real_rist_sender) {
  ristin_cfg_t cfg;
  ristin_t *r;
  struct rist_ctx *sctx;
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;
  struct rist_data_block db;
  char uri[64];
  const char *payload = "hello from a real rist sender";
  char buf[128];
  ssize_t got;
  int tries;

  memset(&cfg, 0, sizeof cfg);
  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%d", TEST_PORT);
  cfg.peer_uri = uri;
  r = ristin_open(&cfg);
  ck_assert_ptr_nonnull(r);

  ck_assert_int_eq(rist_sender_create(&sctx, RIST_PROFILE_SIMPLE, 0, NULL), 0);
  snprintf(uri, sizeof uri, "rist://127.0.0.1:%d", TEST_PORT);
  ck_assert_int_eq(rist_parse_address2(uri, &pc), 0);
  pc->initiate_conn = 1;
  ck_assert_int_eq(rist_peer_create(sctx, &peer, pc), 0);
  rist_peer_config_free2(&pc);
  ck_assert_int_eq(rist_start(sctx), 0);

  memset(&db, 0, sizeof db);
  db.payload = payload;
  db.payload_len = strlen(payload);
  /* handshake needs a moment even on loopback: retry the write until the peer is up */
  for (tries = 0; tries < 50; tries++) {
    if (rist_sender_data_write(sctx, &db) >= 0)
      break;
    usleep(20000);
  }
  ck_assert_int_lt(tries, 50);

  got = read_with_timeout(ristin_fd(r), buf, sizeof buf - 1, 2000);
  ck_assert_int_eq(got, (ssize_t)strlen(payload));
  buf[got] = '\0';
  ck_assert_str_eq(buf, payload);

  rist_destroy(sctx);
  ristin_close(r);
}
END_TEST

static Suite *ristin_suite(void) {
  Suite *s = suite_create("ristin");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rejects_non_rist_scheme);
  tcase_add_test(tc, rejects_missing_at);
  tcase_add_test(tc, receives_payload_from_a_real_rist_sender);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ristin_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
