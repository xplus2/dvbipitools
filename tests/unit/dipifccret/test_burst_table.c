/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/fcc/burst_table.h"

static channel_table_t *g_table;

static channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

static channel_t *make_channel_with_rap(void) {
  channel_t *c;
  unsigned char pkt[188];
  g_table = channel_table_new(1, 0, 8);
  c = lookup_ip(g_table, "239.1.1.1", 5000);
  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  atomic_store_explicit(&c->cache.have_rap, 1, memory_order_relaxed);
  channel_store(g_table, c, 0x1234, 1, 0, 0, pkt, sizeof pkt);
  return c;
}

static void addr_of(struct sockaddr_in *sin, unsigned port) {
  memset(sin, 0, sizeof *sin);
  sin->sin_family = AF_INET;
  sin->sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, "10.0.0.1", &sin->sin_addr);
}

START_TEST(claim_returns_slot_with_zeroed_msn_and_nack_count) {
  burst_table_t *t = burst_table_new(4);
  channel_t *c = make_channel_with_rap();
  burst_t *b = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin;
  burst_slot_t *slot;

  addr_of(&sin, 6000);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, 3, b);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->msn, 0);
  ck_assert_uint_eq(slot->nack_count, 0);
  ck_assert_ptr_eq(slot->b, b);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(find_locates_claimed_slot_by_address) {
  burst_table_t *t = burst_table_new(4);
  channel_t *c = make_channel_with_rap();
  burst_t *b = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin, other;
  burst_slot_t *claimed, *found;

  addr_of(&sin, 6000);
  claimed = burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, 3, b);
  found = burst_table_find(t, (const struct sockaddr *)&sin, sizeof sin);
  ck_assert_ptr_eq(found, claimed);

  addr_of(&other, 6001);
  ck_assert_ptr_null(burst_table_find(t, (const struct sockaddr *)&other, sizeof other));

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(find_returns_null_on_empty_table) {
  burst_table_t *t = burst_table_new(4);
  struct sockaddr_in sin;
  addr_of(&sin, 6000);
  ck_assert_ptr_null(burst_table_find(t, (const struct sockaddr *)&sin, sizeof sin));
  burst_table_free(t);
}
END_TEST

START_TEST(claim_fails_when_table_full) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_with_rap();
  burst_t *b1 = burst_new(c, 1.0, 0, 99);
  burst_t *b2 = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin1, sin2;
  burst_slot_t *slot;

  addr_of(&sin1, 6000);
  addr_of(&sin2, 6001);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin1, sizeof sin1, 3, b1);
  ck_assert_ptr_nonnull(slot);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin2, sizeof sin2, 3, b2);
  ck_assert_ptr_null(slot);

  burst_free(b2); /* claim didn't take ownership, caller must free on rejection */
  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(terminate_marks_burst_done_and_reports_found) {
  burst_table_t *t = burst_table_new(4);
  channel_t *c = make_channel_with_rap();
  burst_t *b = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin, other;

  addr_of(&sin, 6000);
  burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, 3, b);
  ck_assert_int_eq(burst_is_done(b), 0);
  ck_assert_int_eq(burst_table_terminate(t, (const struct sockaddr *)&sin, sizeof sin, 0, 0), 1);
  ck_assert_int_eq(burst_is_done(b), 1);

  addr_of(&other, 6001);
  ck_assert_int_eq(burst_table_terminate(t, (const struct sockaddr *)&other, sizeof other, 0, 0), 0);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

/* has_stop_seq/stop_seqnum reach burst_terminate(): a stop-seq termination waits to
   clip rather than finishing immediately, RFC 6285 Sec 7.5 */
START_TEST(terminate_with_stop_seq_does_not_finish_immediately) {
  burst_table_t *t = burst_table_new(4);
  channel_t *c = make_channel_with_rap();
  burst_t *b = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin;

  addr_of(&sin, 6000);
  burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, 3, b);
  ck_assert_int_eq(burst_table_terminate(t, (const struct sockaddr *)&sin, sizeof sin, 1, 999), 1);
  ck_assert_int_eq(burst_is_done(b), 0);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(claim_reclaims_slot_after_manual_release) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_with_rap();
  burst_t *b1 = burst_new(c, 1.0, 0, 99);
  burst_t *b2 = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin1, sin2;
  burst_slot_t *slot;

  addr_of(&sin1, 6000);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin1, sizeof sin1, 3, b1);
  ck_assert_ptr_nonnull(slot);
  burst_free(b1);
  slot->in_use = 0; /* same release path burst.c's pacer uses on BURST_TICK_DONE */

  addr_of(&sin2, 6001);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin2, sizeof sin2, 3, b2);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->msn, 0); /* reclaimed slot's msn reset, not carried over */

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(target_bps_is_atomically_readable_and_writable_cross_module) {
  channel_t *c = make_channel_with_rap();
  burst_t *b;
  atomic_store_explicit(&c->nominal_bps, 1000000.0, memory_order_relaxed);
  b = burst_new(c, 1.0, 0, 99);
  double orig = atomic_load_explicit(&b->target_bps, memory_order_relaxed);

  ck_assert_double_gt(orig, 0.0);
  atomic_store_explicit(&b->target_bps, orig * 0.5, memory_order_relaxed);
  ck_assert_double_eq_tol(atomic_load_explicit(&b->target_bps, memory_order_relaxed), orig * 0.5, 1e-9);

  burst_free(b);
  channel_table_free(g_table);
}
END_TEST

START_TEST(congestion_adapted_resets_on_claim_and_reclaim) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_with_rap();
  burst_t *b1 = burst_new(c, 1.0, 0, 99);
  burst_t *b2 = burst_new(c, 1.0, 0, 99);
  struct sockaddr_in sin1, sin2;
  burst_slot_t *slot;

  addr_of(&sin1, 6000);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin1, sizeof sin1, 3, b1);
  ck_assert_int_eq(slot->congestion_adapted, 0);
  slot->congestion_adapted = 1;
  burst_free(b1);
  slot->in_use = 0;

  addr_of(&sin2, 6001);
  slot = burst_table_claim(t, (const struct sockaddr *)&sin2, sizeof sin2, 3, b2);
  ck_assert_int_eq(slot->congestion_adapted, 0);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(start_replaces_existing_burst_without_reusing_old_pointer) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_with_rap();
  burst_t *b1 = burst_new(c, 1.0, 0, 99);
  burst_t *b2 = burst_new(c, 1.5, 0, 99);
  struct sockaddr_in sin;
  burst_slot_t *slot;
  uint8_t msn = 99;

  addr_of(&sin, 6000);
  burst_acquire(b1); /* retain a test-owned ref across replacement */
  ck_assert_int_eq(burst_table_start(t, (const struct sockaddr *)&sin, sizeof sin, 3, b1, &msn), 0);
  ck_assert_uint_eq(msn, 0);
  slot = burst_table_find(t, (const struct sockaddr *)&sin, sizeof sin);
  ck_assert_ptr_nonnull(slot);
  ck_assert_ptr_eq(slot->b, b1);

  ck_assert_int_eq(burst_table_start(t, (const struct sockaddr *)&sin, sizeof sin, 4, b2, &msn), 1);
  ck_assert_uint_eq(msn, 1);
  slot = burst_table_find(t, (const struct sockaddr *)&sin, sizeof sin);
  ck_assert_ptr_nonnull(slot);
  ck_assert_ptr_eq(slot->b, b2);
  ck_assert_int_eq(burst_is_done(b1), 1);

  burst_release(b1); /* table swap dropped ownership; test still holds the constructor ref */
  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(note_nack_adapts_then_terminates_under_thresholds) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_with_rap();
  burst_t *b;
  struct sockaddr_in sin;
  burst_table_nack_result_t result;

  atomic_store_explicit(&c->nominal_bps, 4000000.0, memory_order_relaxed);
  b = burst_new(c, 1.0, 0, 99);
  addr_of(&sin, 6000);
  burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, 3, b);

  ck_assert_int_eq(burst_table_note_nack(t, (const struct sockaddr *)&sin, sizeof sin, 4, &result), 1);
  ck_assert_int_eq(result.action, BURST_TABLE_NACK_NONE);

  ck_assert_int_eq(burst_table_note_nack(t, (const struct sockaddr *)&sin, sizeof sin, 4, &result), 1);
  ck_assert_int_eq(result.action, BURST_TABLE_NACK_ADAPTED);
  ck_assert_uint_eq(result.msn, 1);
  ck_assert_double_eq_tol(result.new_bps, 2000000.0, 1e-6);

  ck_assert_int_eq(burst_table_note_nack(t, (const struct sockaddr *)&sin, sizeof sin, 4, &result), 1);
  ck_assert_int_eq(result.action, BURST_TABLE_NACK_NONE);

  ck_assert_int_eq(burst_table_note_nack(t, (const struct sockaddr *)&sin, sizeof sin, 4, &result), 1);
  ck_assert_int_eq(result.action, BURST_TABLE_NACK_TERMINATED);
  ck_assert_int_eq(burst_is_done(b), 1);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

/* race: RAMS-R (burst_table_start) + pacer + NACK + RAMS-T interleaved.
   mirrors main.c pacer_main's acquire/tick/reconcile/release cycle */

#define BURST_RACE_CAP 1u
#define BURST_RACE_RAMS_R_ITERS 3000
#define BURST_RACE_NACK_ITERS 3000
#define BURST_RACE_NACK_THREADS 2
#define BURST_RACE_TERM_ITERS 3000

typedef struct {
  size_t idx;
  burst_t *b;
} burst_race_snap_t;

static burst_table_t *g_burst_race_table;
static channel_t *g_burst_race_chan;
static struct sockaddr_in g_burst_race_addr;
static _Atomic int g_burst_race_stop;
static _Atomic int g_burst_race_bad;

static void burst_race_send_cb(const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)pkt;
  (void)len;
  (void)dscp;
  (void)user;
}

static void *burst_race_rams_r(void *arg) {
  int i;
  (void)arg;
  for (i = 0; i < BURST_RACE_RAMS_R_ITERS; i++) {
    burst_t *b = burst_new(g_burst_race_chan, 1.0, 0, 99);
    uint8_t msn;
    if (!b)
      continue;
    if (burst_table_start(g_burst_race_table, (const struct sockaddr *)&g_burst_race_addr, sizeof g_burst_race_addr, 3, b, &msn) < 0)
      burst_free(b); /* rejected: caller retains sole ownership */
  }
  return NULL;
}

static void *burst_race_nack(void *arg) {
  int i;
  (void)arg;
  for (i = 0; i < BURST_RACE_NACK_ITERS; i++) {
    burst_table_nack_result_t result;
    burst_table_note_nack(g_burst_race_table, (const struct sockaddr *)&g_burst_race_addr, sizeof g_burst_race_addr, 4, &result);
  }
  return NULL;
}

static void *burst_race_terminate(void *arg) {
  int i;
  (void)arg;
  for (i = 0; i < BURST_RACE_TERM_ITERS; i++)
    burst_table_terminate(g_burst_race_table, (const struct sockaddr *)&g_burst_race_addr, sizeof g_burst_race_addr, i & 1, 1000u + (uint32_t)i);
  return NULL;
}

static void *burst_race_pacer(void *arg) {
  burst_table_t *t = g_burst_race_table;
  burst_race_snap_t snap[BURST_RACE_CAP];
  (void)arg;

  while (!atomic_load_explicit(&g_burst_race_stop, memory_order_relaxed)) {
    size_t i, n = 0;

    pthread_mutex_lock(&t->lock);
    for (i = 0; i < t->cap && n < BURST_RACE_CAP; i++) {
      if (!t->slots[i].in_use)
        continue;
      if (!t->slots[i].b) {
        atomic_store_explicit(&g_burst_race_bad, 1, memory_order_relaxed);
        continue;
      }
      burst_acquire(t->slots[i].b);
      snap[n].idx = i;
      snap[n].b = t->slots[i].b;
      n++;
    }
    pthread_mutex_unlock(&t->lock);

    for (i = 0; i < n; i++) {
      burst_tick_result_t r = burst_tick(snap[i].b, 60000, burst_race_send_cb, NULL);
      int remove_slot = 0;

      pthread_mutex_lock(&t->lock);
      if (t->slots[snap[i].idx].in_use && t->slots[snap[i].idx].b == snap[i].b && r == BURST_TICK_DONE) {
        t->slots[snap[i].idx].b = NULL;
        t->slots[snap[i].idx].in_use = 0;
        remove_slot = 1;
      }
      pthread_mutex_unlock(&t->lock);

      if (remove_slot)
        burst_release(snap[i].b); /* drop slot ownership */
      burst_release(snap[i].b); /* drop pacer snapshot */
    }
  }
  return NULL;
}

START_TEST(burst_table_survives_rams_r_pacer_nack_termination_race) {
  pthread_t rams_r_thread, terminate_thread, pacer_thread, nack_threads[BURST_RACE_NACK_THREADS];
  channel_t *c = make_channel_with_rap();
  long i;
  burst_t *seed;
  uint8_t msn;

  g_burst_race_table = burst_table_new(BURST_RACE_CAP);
  g_burst_race_chan = c;
  addr_of(&g_burst_race_addr, 6000);
  atomic_store_explicit(&g_burst_race_stop, 0, memory_order_relaxed);
  atomic_store_explicit(&g_burst_race_bad, 0, memory_order_relaxed);

  seed = burst_new(c, 1.0, 0, 99);
  ck_assert_int_eq(burst_table_start(g_burst_race_table, (const struct sockaddr *)&g_burst_race_addr, sizeof g_burst_race_addr, 3, seed, &msn), 0);

  pthread_create(&pacer_thread, NULL, burst_race_pacer, NULL);
  pthread_create(&rams_r_thread, NULL, burst_race_rams_r, NULL);
  pthread_create(&terminate_thread, NULL, burst_race_terminate, NULL);
  for (i = 0; i < BURST_RACE_NACK_THREADS; i++)
    pthread_create(&nack_threads[i], NULL, burst_race_nack, NULL);

  pthread_join(rams_r_thread, NULL);
  pthread_join(terminate_thread, NULL);
  for (i = 0; i < BURST_RACE_NACK_THREADS; i++)
    pthread_join(nack_threads[i], NULL);

  atomic_store_explicit(&g_burst_race_stop, 1, memory_order_relaxed);
  pthread_join(pacer_thread, NULL);

  ck_assert_int_eq(atomic_load_explicit(&g_burst_race_bad, memory_order_relaxed), 0);

  burst_table_free(g_burst_race_table);
  channel_table_free(g_table);
}
END_TEST

/* race: N threads each claiming a distinct address against cap < N slots,
   TSan-checked. exactly cap must succeed, rest -1, no slot double-claimed */

#define BURST_EXHAUST_CAP 4u
#define BURST_EXHAUST_THREADS 16

typedef struct {
  burst_table_t *t;
  channel_t *c;
  int idx;
  int result;
} burst_exhaust_arg_t;

static void *burst_exhaust_worker(void *arg) {
  burst_exhaust_arg_t *a = arg;
  struct sockaddr_in sin;
  burst_t *b = burst_new(a->c, 1.0, 0, 99);
  uint8_t msn;

  addr_of(&sin, 7000u + (unsigned)a->idx);
  a->result = burst_table_start(a->t, (const struct sockaddr *)&sin, sizeof sin, 3, b, &msn);
  if (a->result < 0)
    burst_free(b);
  return NULL;
}

START_TEST(start_under_concurrent_exhaustion_admits_exactly_cap_sessions) {
  burst_table_t *t = burst_table_new(BURST_EXHAUST_CAP);
  channel_t *c = make_channel_with_rap();
  pthread_t threads[BURST_EXHAUST_THREADS];
  burst_exhaust_arg_t args[BURST_EXHAUST_THREADS];
  int i, claimed = 0, rejected = 0;
  size_t in_use_count = 0;
  unsigned seen_ports[BURST_EXHAUST_CAP];
  size_t seen_n = 0;

  for (i = 0; i < BURST_EXHAUST_THREADS; i++) {
    args[i].t = t;
    args[i].c = c;
    args[i].idx = i;
    args[i].result = -2;
  }
  for (i = 0; i < BURST_EXHAUST_THREADS; i++)
    pthread_create(&threads[i], NULL, burst_exhaust_worker, &args[i]);
  for (i = 0; i < BURST_EXHAUST_THREADS; i++)
    pthread_join(threads[i], NULL);

  for (i = 0; i < BURST_EXHAUST_THREADS; i++) {
    if (args[i].result == 0) {
      claimed++;
    } else {
      ck_assert_int_eq(args[i].result, -1);
      rejected++;
    }
  }
  ck_assert_int_eq(claimed, (int)BURST_EXHAUST_CAP);
  ck_assert_int_eq(rejected, BURST_EXHAUST_THREADS - (int)BURST_EXHAUST_CAP);

  for (i = 0; i < (int)t->cap; i++) {
    const struct sockaddr_in *sin;
    unsigned port;
    size_t j;

    if (!t->slots[i].in_use)
      continue;
    in_use_count++;
    sin = (const struct sockaddr_in *)&t->slots[i].addr;
    port = ntohs(sin->sin_port);
    for (j = 0; j < seen_n; j++)
      ck_assert_uint_ne(seen_ports[j], port);
    seen_ports[seen_n++] = port;
  }
  ck_assert_uint_eq(in_use_count, BURST_EXHAUST_CAP);

  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

static Suite *burst_table_suite(void) {
  Suite *s = suite_create("burst_table");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, claim_returns_slot_with_zeroed_msn_and_nack_count);
  tcase_add_test(tc, find_locates_claimed_slot_by_address);
  tcase_add_test(tc, find_returns_null_on_empty_table);
  tcase_add_test(tc, claim_fails_when_table_full);
  tcase_add_test(tc, terminate_marks_burst_done_and_reports_found);
  tcase_add_test(tc, terminate_with_stop_seq_does_not_finish_immediately);
  tcase_add_test(tc, claim_reclaims_slot_after_manual_release);
  tcase_add_test(tc, target_bps_is_atomically_readable_and_writable_cross_module);
  tcase_add_test(tc, congestion_adapted_resets_on_claim_and_reclaim);
  tcase_add_test(tc, start_replaces_existing_burst_without_reusing_old_pointer);
  tcase_add_test(tc, note_nack_adapts_then_terminates_under_thresholds);
  tcase_add_test(tc, burst_table_survives_rams_r_pacer_nack_termination_race);
  tcase_add_test(tc, start_under_concurrent_exhaustion_admits_exactly_cap_sessions);
  tcase_set_timeout(tc, 20);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(burst_table_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
