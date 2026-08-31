/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/ws/ws_clients.h"

static client_info_t make_info(const char *ip) {
  client_info_t info;
  memset(&info, 0, sizeof info);
  info.ip = ip;
  info.http_ver = 1;
  info.fmt = ROUTE_FMT_HLS;
  return info;
}

START_TEST(touch_same_info_twice_returns_same_handle) {
  client_info_t info = make_info("10.0.0.1");
  int h1, h2;
  ws_clients_init(4);
  h1 = ws_clients_touch(&info);
  h2 = ws_clients_touch(&info);
  ck_assert_int_ge(h1, 0);
  ck_assert_int_eq(h1, h2);
}
END_TEST

START_TEST(remove_evicts_client_from_snapshot) {
  client_info_t info = make_info("10.0.0.2");
  int h;
  char *snap = NULL;
  ws_clients_init(4);
  h = ws_clients_add_persistent(&info);
  ck_assert_int_ge(h, 0);
  ck_assert_int_eq(ws_clients_build_snapshot(&snap), 0);
  ck_assert(strstr(snap, "10.0.0.2") != NULL);

  ws_clients_remove(h);
  ck_assert_int_eq(ws_clients_build_snapshot(&snap), 0);
  ck_assert(strstr(snap, "10.0.0.2") == NULL);
}
END_TEST

/* the fix under test: a handle held past its slot's reuse must not
   touch the new occupant */
START_TEST(stale_remove_handle_does_not_evict_new_occupant) {
  client_info_t info_a = make_info("10.0.0.3");
  client_info_t info_b = make_info("10.0.0.4");
  int h_a, h_b;
  char *snap = NULL;

  ws_clients_init(2);
  h_a = ws_clients_add_persistent(&info_a);
  ck_assert_int_ge(h_a, 0);
  ws_clients_remove(h_a); /* frees the slot h_a pointed at */

  h_b = ws_clients_add_persistent(&info_b); /* claims that same slot */
  ck_assert_int_ge(h_b, 0);

  ws_clients_remove(h_a); /* stale: must be a no-op */

  ck_assert_int_eq(ws_clients_build_snapshot(&snap), 0);
  ck_assert(strstr(snap, "10.0.0.4") != NULL);

  ws_clients_remove(h_b); /* fresh handle: must still work */
  ck_assert_int_eq(ws_clients_build_snapshot(&snap), 0);
  ck_assert(strstr(snap, "10.0.0.4") == NULL);
}
END_TEST

START_TEST(stale_add_bytes_handle_does_not_corrupt_new_occupant) {
  client_info_t info_a = make_info("10.0.0.5");
  client_info_t info_b = make_info("10.0.0.6");
  int h_a, h_b;
  char *snap = NULL;

  ws_clients_init(2);
  h_a = ws_clients_add_persistent(&info_a);
  ws_clients_remove(h_a);
  h_b = ws_clients_add_persistent(&info_b);
  ck_assert_int_ge(h_b, 0);

  ws_clients_add_bytes(h_a, 999999); /* stale: must not touch slot's new owner */

  ck_assert_int_eq(ws_clients_build_snapshot(&snap), 0);
  ck_assert(strstr(snap, "10.0.0.6") != NULL);
}
END_TEST

START_TEST(touch_distinguishes_different_clients) {
  client_info_t info_a = make_info("10.1.0.1");
  client_info_t info_b = make_info("10.1.0.2");
  int h_a, h_b;
  ws_clients_init(8);
  h_a = ws_clients_touch(&info_a);
  h_b = ws_clients_touch(&info_b);
  ck_assert_int_ge(h_a, 0);
  ck_assert_int_ge(h_b, 0);
  ck_assert_int_ne(h_a, h_b);
  ck_assert_int_eq(ws_clients_touch(&info_a), h_a);
  ck_assert_int_eq(ws_clients_touch(&info_b), h_b);
}
END_TEST

START_TEST(touch_finds_correct_client_among_many) {
  char ips[16][32];
  client_info_t infos[16];
  int handles[16];
  int i;

  ws_clients_init(16);
  for (i = 0; i < 16; i++) {
    snprintf(ips[i], sizeof ips[i], "10.2.0.%d", i + 1);
    infos[i] = make_info(ips[i]);
    infos[i].pmt_pid = (unsigned)i;
    handles[i] = ws_clients_touch(&infos[i]);
    ck_assert_int_ge(handles[i], 0);
  }
  for (i = 0; i < 16; i++)
    ck_assert_int_eq(ws_clients_touch(&infos[i]), handles[i]);
}
END_TEST

/* only remaining enforcement point for --max-clients since reactor_accept_setup()'s
   accept-time check (which capped raw HTTP connections, not streams) was removed */
START_TEST(touch_returns_negative_once_registry_full) {
  char ips[4][32];
  client_info_t infos[4];
  client_info_t overflow;
  int i;

  ws_clients_init(4);
  for (i = 0; i < 4; i++) {
    snprintf(ips[i], sizeof ips[i], "10.3.0.%d", i + 1);
    infos[i] = make_info(ips[i]);
    ck_assert_int_ge(ws_clients_touch(&infos[i]), 0);
  }
  overflow = make_info("10.3.0.99");
  ck_assert_int_lt(ws_clients_touch(&overflow), 0);

  /* still-alive existing clients unaffected, still resolve to their handle */
  ck_assert_int_ge(ws_clients_touch(&infos[0]), 0);
}
END_TEST

START_TEST(handle_changes_across_slot_reuse) {
  client_info_t info_a = make_info("10.0.0.7");
  client_info_t info_b = make_info("10.0.0.8");
  int h_a, h_b;

  ws_clients_init(2);
  h_a = ws_clients_add_persistent(&info_a);
  ws_clients_remove(h_a);
  h_b = ws_clients_add_persistent(&info_b); /* same slot, must differ */

  ck_assert_int_ne(h_a, h_b);
}
END_TEST

static Suite *ws_clients_suite(void) {
  Suite *s = suite_create("dipixy_ws_clients");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, touch_same_info_twice_returns_same_handle);
  tcase_add_test(tc, remove_evicts_client_from_snapshot);
  tcase_add_test(tc, stale_remove_handle_does_not_evict_new_occupant);
  tcase_add_test(tc, stale_add_bytes_handle_does_not_corrupt_new_occupant);
  tcase_add_test(tc, touch_distinguishes_different_clients);
  tcase_add_test(tc, touch_finds_correct_client_among_many);
  tcase_add_test(tc, touch_returns_negative_once_registry_full);
  tcase_add_test(tc, handle_changes_across_slot_reuse);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ws_clients_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
