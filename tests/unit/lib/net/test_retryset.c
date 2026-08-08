/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <stdlib.h>

#include "lib/net/retryset.h"

typedef struct {
  int fail_immediately; /* open_start returns NULL */
  int steps_to_done;    /* open_step returns PENDING this many times first */
  int fail_after;       /* 1 = ends in ERROR instead of DONE */
  int open_start_calls;
  int close_calls;
} mock_spec_t;

typedef struct {
  mock_spec_t *spec;
  int steps_remaining;
  int fail;
} mock_opening_t;

typedef struct {
  mock_spec_t *spec;
  int fd;
} mock_result_t;

static void *mock_open_start(void *ctx) {
  mock_spec_t *spec = ctx;
  mock_opening_t *o;

  spec->open_start_calls++;
  if (spec->fail_immediately)
    return NULL;
  o = calloc(1, sizeof *o);
  o->spec = spec;
  o->steps_remaining = spec->steps_to_done;
  o->fail = spec->fail_after;
  return o;
}

static int mock_open_poll_fd(const void *o) {
  (void)o;
  return 99;
}

static short mock_open_poll_events(const void *o) {
  (void)o;
  return 5;
}

static retryset_open_state_t mock_open_step(void *ov) {
  mock_opening_t *o = ov;
  if (o->steps_remaining-- > 0)
    return RETRYSET_OPEN_PENDING;
  return o->fail ? RETRYSET_OPEN_ERROR : RETRYSET_OPEN_DONE;
}

static void *mock_open_take(void *ov) {
  mock_opening_t *o = ov;
  mock_result_t *r = calloc(1, sizeof *r);
  r->spec = o->spec;
  r->fd = 42;
  free(o);
  return r;
}

static void mock_open_free(void *ov) { free(ov); }
static int mock_result_fd(const void *rv) { return ((const mock_result_t *)rv)->fd; }

static void mock_result_close(void *rv) {
  mock_result_t *r = rv;
  r->spec->close_calls++;
  free(r);
}

static const retryset_ops_t mock_ops = {mock_open_start,  mock_open_poll_fd, mock_open_poll_events,
                                         mock_open_step,   mock_open_take,    mock_open_free,
                                         mock_result_fd,   mock_result_close};

START_TEST(retryset_connects_when_steps_to_done_is_zero) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 1);
  ck_assert_ptr_nonnull(rs);
  ck_assert_ptr_null(retryset_result(rs, 0));

  retryset_service(rs, 0, now); /* DOWN -> CONNECTING */
  ck_assert_int_eq(spec.open_start_calls, 1);
  retryset_service(rs, 0, now); /* CONNECTING -> CONNECTED */

  ck_assert_ptr_nonnull(retryset_result(rs, 0));
  ck_assert_int_eq(mock_result_fd(retryset_result(rs, 0)), 42);

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_stays_pending_across_multiple_steps) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;
  int i;

  spec.steps_to_done = 2;
  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 1);

  retryset_service(rs, 0, now); /* starts opening */
  for (i = 0; i < 2; i++) {
    retryset_service(rs, 0, now); /* still PENDING (steps_to_done=2) */
    ck_assert_ptr_null(retryset_result(rs, 0));
  }
  retryset_service(rs, 0, now); /* one more step -> DONE */
  ck_assert_ptr_nonnull(retryset_result(rs, 0));

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_poll_fd_and_events_reflect_state) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  spec.steps_to_done = 1;
  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 1);

  ck_assert_int_eq(retryset_poll_fd(rs, 0), -1); /* DOWN: nothing to wait on */

  retryset_service(rs, 0, now); /* DOWN -> CONNECTING */
  ck_assert_int_eq(retryset_poll_fd(rs, 0), 99);
  ck_assert_int_eq(retryset_poll_events(rs, 0), 5);

  retryset_service(rs, 0, now); /* still PENDING (steps_to_done=1) */
  retryset_service(rs, 0, now); /* -> CONNECTED */
  ck_assert_int_eq(retryset_poll_fd(rs, 0), 42);
  ck_assert_int_eq(retryset_poll_events(rs, 0), POLLIN);

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_connect_failure_reschedules_and_reopens_after_deadline) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  spec.fail_after = 1;
  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 3);

  retryset_service(rs, 0, now); /* DOWN -> CONNECTING */
  retryset_service(rs, 0, now); /* CONNECTING -> ERROR -> DOWN, retry at now+3 */
  ck_assert_int_eq(spec.open_start_calls, 1);
  ck_assert_ptr_null(retryset_result(rs, 0));
  ck_assert_int_eq(retryset_next_deadline(rs), now + 3);

  retryset_service(rs, 0, now); /* deadline not reached yet: no new attempt */
  ck_assert_int_eq(spec.open_start_calls, 1);

  retryset_service(rs, 0, now + 3); /* deadline reached: retries */
  ck_assert_int_eq(spec.open_start_calls, 2);

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_single_slot_never_retries_when_error_retry_s_is_zero) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  spec.fail_immediately = 1;
  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 0);

  retryset_service(rs, 0, now);
  ck_assert_int_eq(retryset_next_deadline(rs), RETRYSET_NEVER);

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_defaults_retry_interval_for_multi_slot_when_zero) {
  mock_spec_t specs[2] = {{0}, {0}};
  void *ctxs[2];
  retryset_t *rs;
  time_t now = 1000;

  specs[0].fail_immediately = 1;
  specs[1].fail_immediately = 1;
  ctxs[0] = &specs[0];
  ctxs[1] = &specs[1];
  rs = retryset_new(2, ctxs, NULL, &mock_ops, 0);

  retryset_service(rs, 0, now);
  retryset_service(rs, 1, now);
  ck_assert_int_eq(retryset_next_deadline(rs), now + 5); /* multi-input default */

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_mark_down_closes_result_and_reschedules) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 4);

  retryset_service(rs, 0, now);
  retryset_service(rs, 0, now);
  ck_assert_ptr_nonnull(retryset_result(rs, 0));

  retryset_mark_down(rs, 0, now);
  ck_assert_ptr_null(retryset_result(rs, 0));
  ck_assert_int_eq(spec.close_calls, 1);
  ck_assert_int_eq(retryset_next_deadline(rs), now + 4);

  retryset_free(rs);
}
END_TEST

START_TEST(retryset_free_closes_still_connected_slots) {
  mock_spec_t spec = {0};
  void *ctxs[1];
  retryset_t *rs;
  time_t now = 1000;

  ctxs[0] = &spec;
  rs = retryset_new(1, ctxs, NULL, &mock_ops, 1);
  retryset_service(rs, 0, now);
  retryset_service(rs, 0, now);
  ck_assert_ptr_nonnull(retryset_result(rs, 0));

  retryset_free(rs);
  ck_assert_int_eq(spec.close_calls, 1);
}
END_TEST

static Suite *retryset_suite(void) {
  Suite *s = suite_create("retryset");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, retryset_connects_when_steps_to_done_is_zero);
  tcase_add_test(tc, retryset_stays_pending_across_multiple_steps);
  tcase_add_test(tc, retryset_poll_fd_and_events_reflect_state);
  tcase_add_test(tc, retryset_connect_failure_reschedules_and_reopens_after_deadline);
  tcase_add_test(tc, retryset_single_slot_never_retries_when_error_retry_s_is_zero);
  tcase_add_test(tc, retryset_defaults_retry_interval_for_multi_slot_when_zero);
  tcase_add_test(tc, retryset_mark_down_closes_result_and_reschedules);
  tcase_add_test(tc, retryset_free_closes_still_connected_slots);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(retryset_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
