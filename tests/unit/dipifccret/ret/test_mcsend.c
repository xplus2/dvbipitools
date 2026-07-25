/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/ret/mcsend.h"

START_TEST(mcsend_ensure_then_get_returns_socket) {
  mcsend_table_t *t = mcsend_table_new(4, NULL, 1);
  channel_t c;
  memset(&c, 0, sizeof c);
  c.family = AF_INET;
  snprintf(c.group, sizeof c.group, "239.1.1.1");
  c.port = 5000;

  ck_assert_ptr_null(mcsend_get(t, &c));
  mcsend_ensure(t, &c, 0);
  ck_assert_ptr_nonnull(mcsend_get(t, &c));

  mcsend_table_free(t);
}
END_TEST

START_TEST(mcsend_ensure_is_idempotent) {
  mcsend_table_t *t = mcsend_table_new(4, NULL, 1);
  channel_t c;
  mcast_t *m1, *m2;
  memset(&c, 0, sizeof c);
  c.family = AF_INET;
  snprintf(c.group, sizeof c.group, "239.1.1.2");
  c.port = 5001;

  mcsend_ensure(t, &c, 0);
  m1 = mcsend_get(t, &c);
  mcsend_ensure(t, &c, 0); /* second call: no second socket */
  m2 = mcsend_get(t, &c);
  ck_assert_ptr_eq(m1, m2);

  mcsend_table_free(t);
}
END_TEST

START_TEST(mcsend_ensure_different_channels_get_different_sockets) {
  mcsend_table_t *t = mcsend_table_new(4, NULL, 1);
  channel_t a, b;
  memset(&a, 0, sizeof a);
  a.family = AF_INET;
  snprintf(a.group, sizeof a.group, "239.1.1.3");
  a.port = 5002;
  memset(&b, 0, sizeof b);
  b.family = AF_INET;
  snprintf(b.group, sizeof b.group, "239.1.1.4");
  b.port = 5003;

  mcsend_ensure(t, &a, 0);
  mcsend_ensure(t, &b, 0);
  ck_assert_ptr_ne(mcsend_get(t, &a), mcsend_get(t, &b));

  mcsend_table_free(t);
}
END_TEST

/* race test: writer publish vs reader lookup, TSan-checked */

#define MCSEND_RACE_READERS 4
#define MCSEND_RACE_ITERS 50000

static mcsend_table_t *g_mc_race_table;
static channel_t g_mc_race_chan;
static _Atomic int g_mc_race_bad;

static void *mc_race_writer(void *arg) {
  (void)arg;
  mcsend_ensure(g_mc_race_table, &g_mc_race_chan, 0);
  return NULL;
}

static void *mc_race_reader(void *arg) {
  int i;
  (void)arg;
  for (i = 0; i < MCSEND_RACE_ITERS; i++) {
    mcast_t *m = mcsend_get(g_mc_race_table, &g_mc_race_chan);
    if (m && mcast_fd(m) < 0)
      atomic_store_explicit(&g_mc_race_bad, 1, memory_order_relaxed);
  }
  return NULL;
}

START_TEST(mcsend_publish_race_no_torn_pointer) {
  pthread_t writer, readers[MCSEND_RACE_READERS];
  long i;

  g_mc_race_table = mcsend_table_new(4, NULL, 1);
  memset(&g_mc_race_chan, 0, sizeof g_mc_race_chan);
  g_mc_race_chan.family = AF_INET;
  snprintf(g_mc_race_chan.group, sizeof g_mc_race_chan.group, "239.1.1.9");
  g_mc_race_chan.port = 5009;
  atomic_store_explicit(&g_mc_race_bad, 0, memory_order_relaxed);

  pthread_create(&writer, NULL, mc_race_writer, NULL);
  for (i = 0; i < MCSEND_RACE_READERS; i++)
    pthread_create(&readers[i], NULL, mc_race_reader, NULL);

  pthread_join(writer, NULL);
  for (i = 0; i < MCSEND_RACE_READERS; i++)
    pthread_join(readers[i], NULL);

  ck_assert_int_eq(atomic_load_explicit(&g_mc_race_bad, memory_order_relaxed), 0);
  ck_assert_ptr_nonnull(mcsend_get(g_mc_race_table, &g_mc_race_chan));

  mcsend_table_free(g_mc_race_table);
}
END_TEST

static Suite *mcsend_suite(void) {
  Suite *s = suite_create("dipifccret_mcsend");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mcsend_ensure_then_get_returns_socket);
  tcase_add_test(tc, mcsend_ensure_is_idempotent);
  tcase_add_test(tc, mcsend_ensure_different_channels_get_different_sockets);
  tcase_add_test(tc, mcsend_publish_race_no_torn_pointer);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mcsend_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
