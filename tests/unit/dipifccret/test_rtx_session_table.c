/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipifccret/ret/rtx_session_table.h"

static struct sockaddr_in mk_addr(unsigned short port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  inet_pton(AF_INET, "10.0.0.1", &a.sin_addr);
  return a;
}

START_TEST(get_finds_existing_session_by_address) {
  rtx_session_table_t *t = rtx_session_table_new(4);
  struct sockaddr_in a = mk_addr(6000);
  rtx_session_slot_t *first, *second;

  first = rtx_session_table_get(t, (const struct sockaddr *)&a, sizeof a);
  ck_assert_ptr_nonnull(first);
  atomic_fetch_add_explicit(&first->seq, 3, memory_order_relaxed);

  second = rtx_session_table_get(t, (const struct sockaddr *)&a, sizeof a);
  ck_assert_ptr_eq(first, second);
  ck_assert_uint_eq(atomic_load_explicit(&second->seq, memory_order_relaxed), 3u);

  rtx_session_table_free(t);
}
END_TEST

/* F.3.2.1: N unicast RET sessions, one per HNED; distinct client addresses must
   never share a slot or perturb each other's seq counter */
START_TEST(get_returns_distinct_independent_sessions_per_address) {
  rtx_session_table_t *t = rtx_session_table_new(4);
  struct sockaddr_in a = mk_addr(6000);
  struct sockaddr_in b = mk_addr(6001);
  rtx_session_slot_t *sa, *sb;

  sa = rtx_session_table_get(t, (const struct sockaddr *)&a, sizeof a);
  sb = rtx_session_table_get(t, (const struct sockaddr *)&b, sizeof b);
  ck_assert_ptr_nonnull(sa);
  ck_assert_ptr_nonnull(sb);
  ck_assert_ptr_ne(sa, sb);
  ck_assert_uint_eq(atomic_load_explicit(&sa->seq, memory_order_relaxed), 0u);
  ck_assert_uint_eq(atomic_load_explicit(&sb->seq, memory_order_relaxed), 0u);

  atomic_fetch_add_explicit(&sa->seq, 1, memory_order_relaxed);
  ck_assert_uint_eq(atomic_load_explicit(&sb->seq, memory_order_relaxed), 0u); /* untouched */

  rtx_session_table_free(t);
}
END_TEST

/* addr NULL/addrlen 0 is a unit-test convenience some ret.c callers exercise
   (see test_ret.c's from=NULL cases); must not crash, must stay self-consistent */
START_TEST(get_tolerates_null_address) {
  rtx_session_table_t *t = rtx_session_table_new(4);
  rtx_session_slot_t *s1 = rtx_session_table_get(t, NULL, 0);
  rtx_session_slot_t *s2 = rtx_session_table_get(t, NULL, 0);
  ck_assert_ptr_nonnull(s1);
  ck_assert_ptr_eq(s1, s2);
  rtx_session_table_free(t);
}
END_TEST

/* no protocol path exists to reject a repair (unlike BURST_TABLE_FULL); full
   table must evict coldest session rather than fail new client outright */
START_TEST(full_table_evicts_coldest_when_claiming_new) {
  rtx_session_table_t *t = rtx_session_table_new(2);
  struct sockaddr_in a = mk_addr(6000);
  struct sockaddr_in b = mk_addr(6001);
  struct sockaddr_in c = mk_addr(6002);
  rtx_session_slot_t *sa, *sb, *sc;

  sa = rtx_session_table_get(t, (const struct sockaddr *)&a, sizeof a);
  sb = rtx_session_table_get(t, (const struct sockaddr *)&b, sizeof b);
  ck_assert_ptr_nonnull(sa);
  ck_assert_ptr_nonnull(sb);
  atomic_store_explicit(&sb->seq, 7, memory_order_relaxed);
  sa->last_seen = 0; /* force a as coldest */
  sc = rtx_session_table_get(t, (const struct sockaddr *)&c, sizeof c); /* table full: evicts a */
  ck_assert_ptr_nonnull(sc);
  ck_assert_uint_eq(atomic_load_explicit(&sc->seq, memory_order_relaxed), 0u); /* fresh session */
  sb = rtx_session_table_get(t, (const struct sockaddr *)&b, sizeof b); /* b survives eviction */
  ck_assert_uint_eq(atomic_load_explicit(&sb->seq, memory_order_relaxed), 7u);

  rtx_session_table_free(t);
}
END_TEST

START_TEST(reap_step_frees_only_stale_slots) {
  rtx_session_table_t *t = rtx_session_table_new(2);
  struct sockaddr_in a = mk_addr(6000);
  struct sockaddr_in b = mk_addr(6001);
  struct sockaddr_in c = mk_addr(6002);
  rtx_session_slot_t *sa, *sb, *sc;

  sa = rtx_session_table_get(t, (const struct sockaddr *)&a, sizeof a);
  sb = rtx_session_table_get(t, (const struct sockaddr *)&b, sizeof b);
  ck_assert_ptr_nonnull(sa);
  ck_assert_ptr_nonnull(sb);
  atomic_store_explicit(&sb->seq, 5, memory_order_relaxed);
  sa->last_seen = 0; /* force-age a well past any max_age_s below */
  rtx_session_table_reap_step(t, 60, 8); /* max_scan covers this 2-slot table entirely */
  sc = rtx_session_table_get(t, (const struct sockaddr *)&c, sizeof c); /* fits without evicting b */
  ck_assert_ptr_nonnull(sc);
  ck_assert_uint_eq(atomic_load_explicit(&sc->seq, memory_order_relaxed), 0u);
  sb = rtx_session_table_get(t, (const struct sockaddr *)&b, sizeof b); /* untouched by reap */
  ck_assert_uint_eq(atomic_load_explicit(&sb->seq, memory_order_relaxed), 5u);
  rtx_session_table_free(t);
}
END_TEST

static Suite *rtx_session_table_suite(void) {
  Suite *s = suite_create("rtx_session_table");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, get_finds_existing_session_by_address);
  tcase_add_test(tc, get_returns_distinct_independent_sessions_per_address);
  tcase_add_test(tc, get_tolerates_null_address);
  tcase_add_test(tc, full_table_evicts_coldest_when_claiming_new);
  tcase_add_test(tc, reap_step_frees_only_stale_slots);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtx_session_table_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
