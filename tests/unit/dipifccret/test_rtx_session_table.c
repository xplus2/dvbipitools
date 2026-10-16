/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
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

#define NEXT(t, a) rtx_session_table_next_seq((t), (const struct sockaddr *)&(a), sizeof(a))

START_TEST(next_seq_advances_per_address) {
  rtx_session_table_t *t = rtx_session_table_new(4);
  struct sockaddr_in a = mk_addr(6000);

  ck_assert_uint_eq(NEXT(t, a), 0u);
  ck_assert_uint_eq(NEXT(t, a), 1u);
  ck_assert_uint_eq(NEXT(t, a), 2u);

  rtx_session_table_free(t);
}
END_TEST

/* F.3.2.1: N unicast RET sessions, one per HNED. distinct client addresses must
   never perturb each other's seq counter. cap well above stripe count: two
   addresses sharing a stripe both survive independently */
START_TEST(next_seq_independent_per_address) {
  rtx_session_table_t *t = rtx_session_table_new(32);
  struct sockaddr_in a = mk_addr(6000);
  struct sockaddr_in b = mk_addr(6001);

  ck_assert_uint_eq(NEXT(t, a), 0u);
  ck_assert_uint_eq(NEXT(t, a), 1u);
  ck_assert_uint_eq(NEXT(t, b), 0u);
  ck_assert_uint_eq(NEXT(t, a), 2u);

  rtx_session_table_free(t);
}
END_TEST

/* addr NULL/addrlen 0 is a unit-test convenience some ret.c callers exercise
   (see test_ret.c's from=NULL cases). one shared session */
START_TEST(next_seq_tolerates_null_address) {
  rtx_session_table_t *t = rtx_session_table_new(4);

  ck_assert_uint_eq(rtx_session_table_next_seq(t, NULL, 0), 0u);
  ck_assert_uint_eq(rtx_session_table_next_seq(t, NULL, 0), 1u);
  rtx_session_table_free(t);
}
END_TEST

/* fills every stripe to guarantee eviction fires somewhere */
#define RTX_TEST_FULL_CAP 16

START_TEST(full_table_evicts_rather_than_rejects_new_client) {
  rtx_session_table_t *t = rtx_session_table_new(RTX_TEST_FULL_CAP);
  struct sockaddr_in addrs[RTX_TEST_FULL_CAP];
  struct sockaddr_in extra = mk_addr(6000 + RTX_TEST_FULL_CAP);
  int i;

  for (i = 0; i < RTX_TEST_FULL_CAP; i++) {
    addrs[i] = mk_addr((unsigned short)(6000 + i));
    NEXT(t, addrs[i]);
  }

  ck_assert_uint_eq(NEXT(t, extra), 0u); /* every stripe now at capacity: fresh session */
  ck_assert_uint_le(rtx_session_table_active_count(t), (size_t)RTX_TEST_FULL_CAP);

  rtx_session_table_free(t);
}
END_TEST

START_TEST(reap_step_frees_stale_and_keeps_fresh) {
  rtx_session_table_t *t = rtx_session_table_new(32);
  struct sockaddr_in a = mk_addr(6000);
  int i;

  NEXT(t, a);
  NEXT(t, a);
  for (i = 0; i < 64; i++)
    rtx_session_table_reap_step(t, 60, 8); /* fresh: kept */
  ck_assert_uint_eq(NEXT(t, a), 2u);

  for (i = 0; i < 64; i++)
    rtx_session_table_reap_step(t, -1, 8); /* everything stale */
  ck_assert_uint_eq(rtx_session_table_active_count(t), 0u);
  ck_assert_uint_eq(NEXT(t, a), 0u); /* reclaimed, restarts */
  rtx_session_table_free(t);
}
END_TEST

START_TEST(active_count_tracks_claims_and_reap) {
  rtx_session_table_t *t = rtx_session_table_new(32);
  struct sockaddr_in a = mk_addr(6000);
  struct sockaddr_in b = mk_addr(6001);
  int i;

  ck_assert_uint_eq(rtx_session_table_active_count(t), 0u);
  NEXT(t, a);
  NEXT(t, b);
  ck_assert_uint_eq(rtx_session_table_active_count(t), 2u);

  for (i = 0; i < 64; i++)
    rtx_session_table_reap_step(t, -1, 8);
  ck_assert_uint_eq(rtx_session_table_active_count(t), 0u);

  rtx_session_table_free(t);
}
END_TEST

static Suite *rtx_session_table_suite(void) {
  Suite *s = suite_create("rtx_session_table");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, next_seq_advances_per_address);
  tcase_add_test(tc, next_seq_independent_per_address);
  tcase_add_test(tc, next_seq_tolerates_null_address);
  tcase_add_test(tc, full_table_evicts_rather_than_rejects_new_client);
  tcase_add_test(tc, reap_step_frees_stale_and_keeps_fresh);
  tcase_add_test(tc, active_count_tracks_claims_and_reap);
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
