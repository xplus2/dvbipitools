/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/qsbr.h"

#define OWNER_TID 0
#define NREADERS 6
#define TEST_FD 7
#define MARKER_LEN 32
#define RUN_MS 500

_Thread_local int t_reactor_tid = -1;
static qsbr_domain_t *g_test_qsbr;
qsbr_domain_t *reactor_qsbr(void) { return g_test_qsbr; }

/* conn.c pool/retire lists: per thread, reactor workers don't quit (but test threads do, freeing their TLS, leaking still-pooled conn_t to LSan */
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
const char *__lsan_default_options(void) { return "detect_leaks=0"; }
#endif

static _Atomic int g_stop;
static _Atomic int g_bad;

/* uniform: every byte equals buf[0]. torn or freed reads break this */
static int uniform(const unsigned char *buf, size_t len) {
  for (size_t i = 1; i < len; i++) if (buf[i] != buf[0]) return 0;
  return 1;
}

static void *owner_thread(void *arg) {
  unsigned char cycle = 0;
  (void)arg;
  t_reactor_tid = OWNER_TID;
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    unsigned char marker[MARKER_LEN];
    conn_t *c = conn_new(TEST_FD, NULL);
    if (!c) continue;
    memset(marker, cycle++, sizeof marker);
    conn_queue(c, marker, sizeof marker);
    conn_publish(c);
    conn_unpublish(c);
    conn_free(c);
    qsbr_worker_quiescent(g_test_qsbr, OWNER_TID);
  }
  return NULL;
}

typedef struct {
  int tid;
} reader_arg_t;

static void *reader_thread(void *arg) {
  const reader_arg_t *ra = arg;
  t_reactor_tid = ra->tid;
  while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
    conn_t *c = conn_for_fd(TEST_FD);
    if (c) {
      size_t len = c->out.len;
      if (len > MARKER_LEN || !uniform(c->out.buf, len)) atomic_store_explicit(&g_bad, 1, memory_order_relaxed);
    }
    qsbr_worker_quiescent(g_test_qsbr, ra->tid);
  }
  return NULL;
}

START_TEST(concurrent_readers_never_see_torn_or_freed_conn_data) {
  pthread_t owner;
  pthread_t readers[NREADERS];
  reader_arg_t rargs[NREADERS];

  ck_assert_int_eq(conn_table_init(TEST_FD + 1), 0);
  g_test_qsbr = qsbr_domain_create(1 + NREADERS);
  ck_assert_ptr_nonnull(g_test_qsbr);
  atomic_store_explicit(&g_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&g_bad, 0, memory_order_relaxed);

  pthread_create(&owner, NULL, owner_thread, NULL);
  for (int i = 0; i < NREADERS; i++) {
    rargs[i].tid = OWNER_TID + 1 + i;
    pthread_create(&readers[i], NULL, reader_thread, &rargs[i]);
  }
  usleep(RUN_MS * 1000);
  atomic_store_explicit(&g_stop, 1, memory_order_relaxed);
  pthread_join(owner, NULL);
  for (int i = 0; i < NREADERS; i++) pthread_join(readers[i], NULL);
  ck_assert_int_eq(atomic_load_explicit(&g_bad, memory_order_relaxed), 0);
}
END_TEST

static Suite *conn_concurrency_suite(void) {
  Suite *s = suite_create("dipixy_conn_concurrency");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, concurrent_readers_never_see_torn_or_freed_conn_data);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(conn_concurrency_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
