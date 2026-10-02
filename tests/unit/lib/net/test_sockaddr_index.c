/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/sockaddr_index.h"

static struct sockaddr_in v4(const char *ip, unsigned port) {
  struct sockaddr_in a;

  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, ip, &a.sin_addr);
  return a;
}

static struct sockaddr_in6 v6(const char *ip, unsigned port) {
  struct sockaddr_in6 a;

  memset(&a, 0, sizeof a);
  a.sin6_family = AF_INET6;
  a.sin6_port = htons((uint16_t)port);
  inet_pton(AF_INET6, ip, &a.sin6_addr);
  return a;
}

#define SA(x) ((const struct sockaddr *)&(x))

START_TEST(v4_insert_find_remove) {
  sockaddr_index_t *idx = sockaddr_index_new(8);
  struct sockaddr_in a = v4("192.0.2.1", 5000);
  struct sockaddr_in other = v4("192.0.2.2", 5000);

  ck_assert_ptr_nonnull(idx);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
  sockaddr_index_insert(idx, SA(a), sizeof a, 3);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), 3u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(other), sizeof other), SIZE_MAX);
  sockaddr_index_remove(idx, SA(a), sizeof a);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
  sockaddr_index_remove(idx, SA(a), sizeof a);
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(same_address_different_port_are_distinct_keys) {
  sockaddr_index_t *idx = sockaddr_index_new(8);
  struct sockaddr_in a = v4("192.0.2.1", 5000);
  struct sockaddr_in b = v4("192.0.2.1", 5001);

  sockaddr_index_insert(idx, SA(a), sizeof a, 1);
  sockaddr_index_insert(idx, SA(b), sizeof b, 2);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), 1u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(b), sizeof b), 2u);
  sockaddr_index_remove(idx, SA(a), sizeof a);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(b), sizeof b), 2u);
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(v6_keys_compare_address_and_port) {
  sockaddr_index_t *idx = sockaddr_index_new(8);
  struct sockaddr_in6 a = v6("2001:db8::1", 6000);
  struct sockaddr_in6 same_port_other_addr = v6("2001:db8::2", 6000);
  struct sockaddr_in6 same_addr_other_port = v6("2001:db8::1", 6001);

  sockaddr_index_insert(idx, SA(a), sizeof a, 7);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), 7u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(same_port_other_addr), sizeof a), SIZE_MAX);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(same_addr_other_port), sizeof a), SIZE_MAX);
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(v4_and_v6_do_not_collide) {
  sockaddr_index_t *idx = sockaddr_index_new(8);
  struct sockaddr_in a = v4("0.0.0.1", 80);
  struct sockaddr_in6 b = v6("::1", 80);

  sockaddr_index_insert(idx, SA(a), sizeof a, 1);
  sockaddr_index_insert(idx, SA(b), sizeof b, 2);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), 1u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(b), sizeof b), 2u);
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(null_zero_length_and_unknown_family_share_one_key) {
  sockaddr_index_t *idx = sockaddr_index_new(8);
  struct sockaddr_in a = v4("192.0.2.1", 1);
  struct sockaddr_in short_v4 = v4("192.0.2.1", 1);
  struct sockaddr un;

  memset(&un, 0, sizeof un);
  un.sa_family = AF_UNIX;
  sockaddr_index_insert(idx, NULL, 0, 9);
  ck_assert_uint_eq(sockaddr_index_find(idx, NULL, 0), 9u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(un), sizeof un), 9u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(short_v4), 4), 9u);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
  sockaddr_index_remove(idx, NULL, 0);
  ck_assert_uint_eq(sockaddr_index_find(idx, NULL, 0), SIZE_MAX);
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(fill_to_capacity_then_churn_keeps_every_key) {
  enum { CAP = 64 };
  sockaddr_index_t *idx = sockaddr_index_new(CAP);
  struct sockaddr_in a;
  size_t i;
  size_t round;

  for (i = 0; i < CAP; i++) {
    a = v4("198.51.100.7", 1000 + (unsigned)i);
    sockaddr_index_insert(idx, SA(a), sizeof a, i);
  }
  for (i = 0; i < CAP; i++) {
    a = v4("198.51.100.7", 1000 + (unsigned)i);
    ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), i);
  }
  for (round = 0; round < 20; round++) {
    for (i = 0; i < CAP; i += 2) {
      a = v4("198.51.100.7", 1000 + (unsigned)i);
      sockaddr_index_remove(idx, SA(a), sizeof a);
    }
    for (i = 0; i < CAP; i += 2) {
      a = v4("198.51.100.7", 1000 + (unsigned)i);
      ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
      sockaddr_index_insert(idx, SA(a), sizeof a, i + 1000 * round);
    }
    for (i = 0; i < CAP; i++) {
      a = v4("198.51.100.7", 1000 + (unsigned)i);
      ck_assert_uint_ne(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
    }
  }
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(removal_rebuild_preserves_survivors_and_misses_stay_misses) {
  enum { N = 40 };
  sockaddr_index_t *idx = sockaddr_index_new(N);
  struct sockaddr_in a;
  size_t i;

  for (i = 0; i < N; i++) {
    a = v4("203.0.113.9", 2000 + (unsigned)i);
    sockaddr_index_insert(idx, SA(a), sizeof a, i);
  }
  for (i = 0; i < N - 4; i++) {
    a = v4("203.0.113.9", 2000 + (unsigned)i);
    sockaddr_index_remove(idx, SA(a), sizeof a);
  }
  for (i = 0; i < N; i++) {
    a = v4("203.0.113.9", 2000 + (unsigned)i);
    if (i < N - 4)
      ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), SIZE_MAX);
    else
      ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), i);
  }
  sockaddr_index_free(idx);
}
END_TEST

START_TEST(tiny_capacity_still_works) {
  sockaddr_index_t *idx = sockaddr_index_new(0);
  struct sockaddr_in a = v4("192.0.2.5", 9);

  ck_assert_ptr_nonnull(idx);
  sockaddr_index_insert(idx, SA(a), sizeof a, 4);
  ck_assert_uint_eq(sockaddr_index_find(idx, SA(a), sizeof a), 4u);
  sockaddr_index_free(idx);
  sockaddr_index_free(NULL);
}
END_TEST

START_TEST(stripe_is_stable_in_range_and_zero_for_null) {
  struct sockaddr_in a = v4("192.0.2.1", 5000);
  struct sockaddr_in6 b = v6("2001:db8::9", 5000);
  size_t s1 = sockaddr_stripe_of(SA(a), sizeof a, 16);
  size_t s2 = sockaddr_stripe_of(SA(b), sizeof b, 16);

  ck_assert_uint_lt(s1, 16u);
  ck_assert_uint_lt(s2, 16u);
  ck_assert_uint_eq(s1, sockaddr_stripe_of(SA(a), sizeof a, 16));
  ck_assert_uint_eq(sockaddr_stripe_of(NULL, 0, 16), 0u);
  ck_assert_uint_eq(sockaddr_stripe_of(SA(a), 0, 16), 0u);
  ck_assert_uint_eq(sockaddr_stripe_of(SA(a), sizeof a, 1), 0u);
}
END_TEST

static Suite *sockaddr_index_suite(void) {
  Suite *s = suite_create("sockaddr_index");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, v4_insert_find_remove);
  tcase_add_test(tc, same_address_different_port_are_distinct_keys);
  tcase_add_test(tc, v6_keys_compare_address_and_port);
  tcase_add_test(tc, v4_and_v6_do_not_collide);
  tcase_add_test(tc, null_zero_length_and_unknown_family_share_one_key);
  tcase_add_test(tc, fill_to_capacity_then_churn_keeps_every_key);
  tcase_add_test(tc, removal_rebuild_preserves_survivors_and_misses_stay_misses);
  tcase_add_test(tc, tiny_capacity_still_works);
  tcase_add_test(tc, stripe_is_stable_in_range_and_zero_for_null);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(sockaddr_index_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
