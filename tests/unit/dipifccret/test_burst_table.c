/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
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
