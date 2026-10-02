/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/tvhead/mpts/priv.h"

#define FAKE_FD 77
#define FAKE_EVENTS 4
#define RETRY_S 1
#define T0 100

struct tvsrc_open {
  int unused;
};

typedef struct {
  int start_fails;
  net_err_reason_t start_reason;
  tvsrc_open_state_t step_state;
  net_err_reason_t step_reason;
  int starts;
  int live_handles;
  int taken;
  int closed;
} fake_t;

static fake_t fake;
static char fake_src_obj;

tvsrc_open_t *tvsrc_open_async_start(const config_t *cfg, const dipitvhead_input_t *input, net_err_reason_t *reason_out) {
  tvsrc_open_t *o;

  (void)cfg;
  (void)input;
  fake.starts++;
  if (fake.start_fails) {
    *reason_out = fake.start_reason;
    return NULL;
  }
  o = calloc(1, sizeof *o);
  fake.live_handles++;
  return o;
}

int tvsrc_open_async_poll_fd(const tvsrc_open_t *o) {
  (void)o;
  return FAKE_FD;
}

short tvsrc_open_async_poll_events(const tvsrc_open_t *o) {
  (void)o;
  return FAKE_EVENTS;
}

tvsrc_open_state_t tvsrc_open_async_step(tvsrc_open_t *o, net_err_reason_t *reason_out) {
  (void)o;
  if (fake.step_state == TVSRC_OPEN_ERROR) *reason_out = fake.step_reason;
  return fake.step_state;
}

tvsrc_t *tvsrc_open_async_take(tvsrc_open_t *o) {
  free(o);
  fake.live_handles--;
  fake.taken++;
  return (tvsrc_t *)&fake_src_obj;
}

void tvsrc_open_async_free(tvsrc_open_t *o) {
  free(o);
  fake.live_handles--;
}

int tvsrc_fd(const tvsrc_t *s) {
  (void)s;
  return FAKE_FD;
}

void tvsrc_close(tvsrc_t *s) {
  (void)s;
  fake.closed++;
}

typedef struct {
  config_t cfg;
  dipitvhead_input_t input;
  input_metrics_t im;
  tv_slot_ctx_t slot;
  void *slot_ptrs[1];
  retryset_t *rs;
} rig_t;

static void rig_init(rig_t *g, int with_metrics) {
  memset(g, 0, sizeof *g);
  memset(&fake, 0, sizeof fake);
  g->slot.cfg = &g->cfg;
  g->slot.input = &g->input;
  g->slot.im = with_metrics ? &g->im : NULL;
  g->slot_ptrs[0] = &g->slot;
  g->rs = retryset_new(1, g->slot_ptrs, NULL, &tv_retry_ops, RETRY_S);
  ck_assert_ptr_nonnull(g->rs);
}

static void rig_free(rig_t *g) {
  retryset_free(g->rs);
  ck_assert_int_eq(fake.live_handles, 0);
}

START_TEST(open_completing_on_first_step_marks_the_input_up) {
  rig_t g;

  rig_init(&g, 1);
  fake.step_state = TVSRC_OPEN_DONE;
  retryset_service(g.rs, 0, T0);
  ck_assert_ptr_null(retryset_result(g.rs, 0));
  retryset_service(g.rs, 0, T0);
  ck_assert_ptr_eq(retryset_result(g.rs, 0), &fake_src_obj);
  ck_assert_int_eq(fake.taken, 1);
  ck_assert_int_eq(g.im.up, 1);
  ck_assert_int_eq(g.im.seen_open, 1);
  ck_assert_uint_eq(g.im.reconnects_total, 0u);
  rig_free(&g);
}
END_TEST

START_TEST(pending_open_exposes_the_poll_target_and_stays_down) {
  rig_t g;

  rig_init(&g, 1);
  fake.step_state = TVSRC_OPEN_PENDING;
  retryset_service(g.rs, 0, T0);
  retryset_service(g.rs, 0, T0);
  ck_assert_ptr_null(retryset_result(g.rs, 0));
  ck_assert_int_eq(retryset_poll_fd(g.rs, 0), FAKE_FD);
  ck_assert_int_eq(retryset_poll_events(g.rs, 0), FAKE_EVENTS);
  ck_assert_int_eq(g.im.up, 0);
  ck_assert_int_eq(fake.live_handles, 1);
  rig_free(&g);
}
END_TEST

START_TEST(step_error_counts_the_reason_and_releases_the_handle) {
  rig_t g;
  net_err_reason_t reason = (net_err_reason_t)_i;

  rig_init(&g, 1);
  fake.step_state = TVSRC_OPEN_ERROR;
  fake.step_reason = reason;
  retryset_service(g.rs, 0, T0);
  retryset_service(g.rs, 0, T0);
  ck_assert_ptr_null(retryset_result(g.rs, 0));
  ck_assert_uint_eq(g.im.errors_total[reason], 1ULL);
  ck_assert_int_eq(g.im.up, 0);
  ck_assert_int_eq(g.im.seen_open, 0);
  rig_free(&g);
}
END_TEST

START_TEST(start_failure_counts_the_reason_and_retries_later) {
  rig_t g;
  net_err_reason_t reason = (net_err_reason_t)_i;

  rig_init(&g, 1);
  fake.start_fails = 1;
  fake.start_reason = reason;
  retryset_service(g.rs, 0, T0);
  ck_assert_uint_eq(g.im.errors_total[reason], 1ULL);
  retryset_service(g.rs, 0, T0);
  ck_assert_int_eq(fake.starts, 1);
  retryset_service(g.rs, 0, T0 + RETRY_S);
  ck_assert_int_eq(fake.starts, 2);
  ck_assert_uint_eq(g.im.errors_total[reason], 2ULL);
  rig_free(&g);
}
END_TEST

START_TEST(reconnect_after_read_failure_counts_a_reconnect) {
  rig_t g;

  rig_init(&g, 1);
  fake.step_state = TVSRC_OPEN_DONE;
  retryset_service(g.rs, 0, T0);
  retryset_service(g.rs, 0, T0);
  ck_assert_ptr_nonnull(retryset_result(g.rs, 0));
  retryset_mark_down(g.rs, 0, T0);
  ck_assert_int_eq(fake.closed, 1);
  retryset_service(g.rs, 0, T0 + RETRY_S);
  retryset_service(g.rs, 0, T0 + RETRY_S);
  ck_assert_ptr_nonnull(retryset_result(g.rs, 0));
  ck_assert_uint_eq(g.im.reconnects_total, 1ULL);
  ck_assert_int_eq(g.im.up, 1);
  rig_free(&g);
}
END_TEST

START_TEST(slots_without_metrics_open_and_fail_safely) {
  rig_t g;

  rig_init(&g, 0);
  fake.start_fails = 1;
  fake.start_reason = NET_ERR_CONNECT;
  retryset_service(g.rs, 0, T0);
  fake.start_fails = 0;
  fake.step_state = TVSRC_OPEN_ERROR;
  fake.step_reason = NET_ERR_CONNECT;
  retryset_service(g.rs, 0, T0 + RETRY_S);
  retryset_service(g.rs, 0, T0 + RETRY_S);
  fake.step_state = TVSRC_OPEN_DONE;
  retryset_service(g.rs, 0, T0 + 2 * RETRY_S);
  retryset_service(g.rs, 0, T0 + 2 * RETRY_S);
  ck_assert_ptr_nonnull(retryset_result(g.rs, 0));
  rig_free(&g);
}
END_TEST

START_TEST(freeing_the_set_while_opening_releases_the_handle) {
  rig_t g;

  rig_init(&g, 1);
  fake.step_state = TVSRC_OPEN_PENDING;
  retryset_service(g.rs, 0, T0);
  ck_assert_int_eq(fake.live_handles, 1);
  rig_free(&g);
}
END_TEST

static Suite *adapter_suite(void) {
  Suite *s = suite_create("dipitvhead_retryset_adapter");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, open_completing_on_first_step_marks_the_input_up);
  tcase_add_test(tc, pending_open_exposes_the_poll_target_and_stays_down);
  tcase_add_loop_test(tc, step_error_counts_the_reason_and_releases_the_handle, 0, NET_ERR_COUNT);
  tcase_add_loop_test(tc, start_failure_counts_the_reason_and_retries_later, 0, NET_ERR_COUNT);
  tcase_add_test(tc, reconnect_after_read_failure_counts_a_reconnect);
  tcase_add_test(tc, slots_without_metrics_open_and_fail_safely);
  tcase_add_test(tc, freeing_the_set_while_opening_releases_the_handle);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(adapter_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
