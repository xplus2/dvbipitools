/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/net/rist/ristin.h"

#ifndef RIST_SEND_HELPER_PATH
#error "RIST_SEND_HELPER_PATH must be defined to the built rist_send_helper binary's path"
#endif

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
  char uri[64];
  const char *payload = "hello from a real rist sender";
  char buf[128];
  ssize_t got;
  pid_t pid;
  int status;

  memset(&cfg, 0, sizeof cfg);
  snprintf(uri, sizeof uri, "rist://@127.0.0.1:%d", TEST_PORT);
  cfg.peer_uri = uri;
  r = ristin_open(&cfg);
  ck_assert_ptr_nonnull(r);

  /* separate process: avoids two rist_ctx's in one process, see rist_send_helper.c */
  pid = fork();
  ck_assert_int_ge(pid, 0);
  if (pid == 0) {
    snprintf(uri, sizeof uri, "rist://127.0.0.1:%d", TEST_PORT);
    execl(RIST_SEND_HELPER_PATH, RIST_SEND_HELPER_PATH, uri, payload, (char *)NULL);
    _exit(127); /* exec failed */
  }

  got = read_with_timeout(ristin_fd(r), buf, sizeof buf - 1, 2000);
  ck_assert_int_eq(got, (ssize_t)strlen(payload));
  buf[got] = '\0';
  ck_assert_str_eq(buf, payload);

  ck_assert_int_eq(waitpid(pid, &status, 0), pid);
  ck_assert_int_eq(WIFEXITED(status) ? WEXITSTATUS(status) : -1, 0);

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
