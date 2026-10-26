/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/net/announce_driver.h"
#include "lib/sys/signal.h"

typedef struct {
  int ready;
  int reloads;
  int cycles;
  int cleanups;
  int fail_at;
  int stop_at;
  int hup_at;
  unsigned busy_ms; /* cycle duration */
  double start[4];  /* mono time of cycles 1..4 */
} drv_t;

static void on_ready(void *ctx, const mcast_t *m) {
  drv_t *d = ctx;
  d->ready++;
  (void)m;
}

static void on_reload(void *ctx) { ((drv_t *)ctx)->reloads++; }

static int on_cycle(void *ctx, mcast_t *m, unsigned cycle) {
  drv_t *d = ctx;
  (void)m;
  d->cycles++;
  ck_assert_uint_eq(cycle, (unsigned)d->cycles);
  if (d->cycles <= 4) d->start[d->cycles - 1] = mono_seconds();
  if (d->busy_ms) usleep(d->busy_ms * 1000u);
  if (d->hup_at && d->cycles == d->hup_at) raise(SIGHUP);
  if (d->stop_at && d->cycles == d->stop_at) signal_request_stop();
  return d->fail_at && d->cycles == d->fail_at;
}

static void on_cleanup(void *ctx) { ((drv_t *)ctx)->cleanups++; }

static announce_driver_t make(drv_t *d, const char *group) {
  announce_driver_t a;
  memset(&a, 0, sizeof a);
  a.ctx = d;
  a.on_ready = on_ready;
  a.reload = on_reload;
  a.run_cycle = on_cycle;
  a.cleanup = on_cleanup;
  a.family = AF_INET;
  a.mcast_group = group;
  a.mcast_port = 5000;
  a.dscp = 0;
  a.interval_s = 0;
  return a;
}

START_TEST(driver_runs_cycles_until_stop_and_reloads_on_hup) {
  drv_t d = {0};
  announce_driver_t a = make(&d, "239.1.2.3");
  signals_install();
  d.stop_at = 3;
  d.hup_at = 1;
  ck_assert_int_eq(announce_driver_run(&a), 0);
  ck_assert_int_eq(d.ready, 1);
  ck_assert_int_eq(d.cycles, 3);
  ck_assert_int_eq(d.cleanups, 1);
  ck_assert_int_ge(d.reloads, 1);
}
END_TEST

START_TEST(driver_stops_on_failing_cycle_and_skips_missing_on_ready) {
  drv_t d = {0};
  announce_driver_t a = make(&d, "239.1.2.3");
  signals_install();
  a.on_ready = NULL;
  d.fail_at = 2;
  ck_assert_int_eq(announce_driver_run(&a), 1);
  ck_assert_int_eq(d.cycles, 2);
  ck_assert_int_eq(d.ready, 0);
  ck_assert_int_eq(d.cleanups, 1);
}
END_TEST

START_TEST(driver_reports_group_that_cannot_open) {
  drv_t d = {0};
  announce_driver_t a = make(&d, "not-an-address");
  ck_assert_int_eq(announce_driver_run(&a), 1);
  ck_assert_int_eq(d.cycles, 0);
  ck_assert_int_eq(d.cleanups, 1);
}
END_TEST

/* fixed rate: cycle time inside interval, no drift */
START_TEST(driver_keeps_interval_including_cycle_time) {
  drv_t d = {0};
  announce_driver_t a = make(&d, "239.1.2.3");
  signals_install();
  a.interval_s = 1;
  d.busy_ms = 400;
  d.stop_at = 3;
  ck_assert_int_eq(announce_driver_run(&a), 0);
  ck_assert_int_eq(d.cycles, 3);
  ck_assert_double_lt(d.start[2] - d.start[0], 2.3);
  ck_assert_double_ge(d.start[2] - d.start[0], 1.9);
}
END_TEST

static Suite *driver_suite(void) {
  Suite *s = suite_create("announce_driver");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, driver_runs_cycles_until_stop_and_reloads_on_hup);
  tcase_add_test(tc, driver_stops_on_failing_cycle_and_skips_missing_on_ready);
  tcase_add_test(tc, driver_reports_group_that_cannot_open);
  tcase_add_test(tc, driver_keeps_interval_including_cycle_time);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(driver_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
