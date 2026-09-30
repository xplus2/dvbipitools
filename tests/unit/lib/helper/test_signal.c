/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/signal.h"

static double elapsed_since(const struct timespec *start) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

START_TEST(wake_fd_becomes_readable_on_stop) {
  struct pollfd pfd;

  signals_install();
  ck_assert_int_ge(signal_wake_fd(), 0);

  pfd.fd = signal_wake_fd();
  pfd.events = POLLIN;
  pfd.revents = 0;
  ck_assert_int_eq(poll(&pfd, 1, 0), 0);

  raise(SIGTERM);
  ck_assert_int_eq(signal_stop_requested(), 1);

  pfd.revents = 0;
  ck_assert_int_eq(poll(&pfd, 1, 0), 1);
  ck_assert_int_ne(pfd.revents & POLLIN, 0);
}
END_TEST

START_TEST(sleep_interruptible_wakes_promptly_on_stop) {
  struct timespec t0;
  double elapsed_s;

  signals_install();
  clock_gettime(CLOCK_MONOTONIC, &t0);
  raise(SIGTERM);
  sleep_interruptible(30.0);
  elapsed_s = elapsed_since(&t0);
  ck_assert_double_lt(elapsed_s, 1.0);
}
END_TEST

START_TEST(sleep_interruptible_returns_after_duration_without_stop) {
  struct timespec t0;
  double elapsed_s;

  clock_gettime(CLOCK_MONOTONIC, &t0);
  sleep_interruptible(0.05);
  elapsed_s = elapsed_since(&t0);
  ck_assert_double_ge(elapsed_s, 0.05);
  ck_assert_double_lt(elapsed_s, 1.0);
}
END_TEST

static void *sleeper_thread(void *arg) {
  const double *secs = arg;
  sleep_interruptible(*secs);
  return NULL;
}

START_TEST(sleep_interruptible_on_another_thread_wakes_on_stop_from_this_one) {
  pthread_t th;
  double secs = 30.0;
  struct timespec t0;
  double elapsed_s;

  signals_install();
  clock_gettime(CLOCK_MONOTONIC, &t0);
  ck_assert_int_eq(pthread_create(&th, NULL, sleeper_thread, &secs), 0);
  usleep(20000);
  raise(SIGTERM);
  pthread_join(th, NULL);
  elapsed_s = elapsed_since(&t0);
  ck_assert_double_lt(elapsed_s, 1.0);
}
END_TEST

START_TEST(wake_fd_is_negative_before_signals_install) {
  ck_assert_int_lt(signal_wake_fd(), 0);
}
END_TEST

static Suite *signal_suite(void) {
  Suite *s = suite_create("lib_helper_signal");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, wake_fd_is_negative_before_signals_install);
  tcase_add_test(tc, wake_fd_becomes_readable_on_stop);
  tcase_add_test(tc, sleep_interruptible_returns_after_duration_without_stop);
  tcase_add_test(tc, sleep_interruptible_wakes_promptly_on_stop);
  tcase_add_test(tc, sleep_interruptible_on_another_thread_wakes_on_stop_from_this_one);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(signal_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
