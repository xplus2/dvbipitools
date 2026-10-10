/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/ws/ws_clients.h"
#include "dipixy/ws/ws_clients_int.h"

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

START_TEST(registry_without_capacity_rejects_everything) {
  client_info_t info = make_info("10.0.0.1");

  ws_clients_init(0);
  ck_assert_int_lt(ws_clients_touch(&info), 0);
  ck_assert_int_lt(ws_clients_add_persistent(&info), 0);
  ws_clients_remove(-1);
  ws_clients_remove(5);
  ws_clients_add_bytes(-1, 10);
  ws_clients_add_bytes(7, 10);
}
END_TEST

typedef struct {
  const char *name;
  void (*mutate)(client_info_t *info);
} field_case_t;

static pid_filter_t g_filter;

static void mut_ip(client_info_t *i) { i->ip = "10.0.0.2"; }
static void mut_fmt(client_info_t *i) { i->fmt = ROUTE_FMT_DASH; }
static void mut_pmt(client_info_t *i) { i->pmt_pid = 0x200; }
static void mut_proto(client_info_t *i) { i->src_proto = "srt"; }
static void mut_addr(client_info_t *i) { i->src_addr = "239.1.1.2:5000"; }
static void mut_ordinal(client_info_t *i) { i->src_ordinal = 2; }
static void mut_name(client_info_t *i) { i->src_name = "other"; }
static void mut_item_num(client_info_t *i) { i->item_num = 3; }
static void mut_item_name(client_info_t *i) { i->item_name = "other item"; }
static void mut_filter(client_info_t *i) {
  pid_filter_parse("0x100", &g_filter);
  i->filter = &g_filter;
}

static const field_case_t field_cases[] = {
  {"ip", mut_ip},
  {"format", mut_fmt},
  {"pmt pid", mut_pmt},
  {"source protocol", mut_proto},
  {"source address", mut_addr},
  {"source ordinal", mut_ordinal},
  {"source name", mut_name},
  {"item number", mut_item_num},
  {"item name", mut_item_name},
  {"pid filter", mut_filter},
};

START_TEST(touch_separates_clients_that_differ_in_any_single_field) {
  const field_case_t *c = &field_cases[_i];
  client_info_t base = make_info("10.0.0.1");
  client_info_t other;
  int h_base;
  int h_other;

  base.src_proto = "udp";
  base.src_addr = "239.1.1.1:5000";
  base.src_ordinal = 1;
  base.src_name = "chan";
  base.item_num = 2;
  base.item_name = "item";
  base.pmt_pid = 0x100;
  other = base;
  c->mutate(&other);
  ws_clients_init(16);
  h_base = ws_clients_touch(&base);
  h_other = ws_clients_touch(&other);
  ck_assert_int_ge(h_base, 0);
  ck_assert_msg(h_other >= 0 && h_other != h_base, "%s: collapsed", c->name);
  ck_assert_int_eq(ws_clients_touch(&base), h_base);
  ck_assert_int_eq(ws_clients_touch(&other), h_other);
}
END_TEST

START_TEST(persistent_clients_are_never_matched_by_touch) {
  client_info_t info = make_info("10.0.0.1");
  int hp;
  int ht;

  ws_clients_init(4);
  hp = ws_clients_add_persistent(&info);
  ht = ws_clients_touch(&info);
  ck_assert_int_ge(hp, 0);
  ck_assert_int_ge(ht, 0);
  ck_assert_int_ne(hp, ht);
}
END_TEST

START_TEST(persistent_registrations_fill_up_and_release_slots) {
  client_info_t a = make_info("10.0.0.1");
  client_info_t b = make_info("10.0.0.2");
  client_info_t c = make_info("10.0.0.3");
  int ha;
  int hb;

  ws_clients_init(2);
  ha = ws_clients_add_persistent(&a);
  hb = ws_clients_add_persistent(&b);
  ck_assert_int_ge(ha, 0);
  ck_assert_int_ge(hb, 0);
  ck_assert_int_lt(ws_clients_add_persistent(&c), 0);
  ws_clients_remove(ha);
  ck_assert_int_ge(ws_clients_add_persistent(&c), 0);
}
END_TEST

START_TEST(removed_slots_are_rehashed_and_survivors_still_resolve) {
  client_info_t a = make_info("10.9.0.1");
  client_info_t b = make_info("10.9.0.2");
  int ha;
  int hb;

  ws_clients_init(2);
  ha = ws_clients_touch(&a);
  hb = ws_clients_touch(&b);
  ck_assert_int_ge(ha, 0);
  ck_assert_int_ge(hb, 0);
  ws_clients_remove(ha);
  for (int i = 0; i < g_stripe_count; i++) {
    pthread_mutex_lock(&g_stripes[i].lock);
    stripe_rehash_if_needed(&g_stripes[i]);
    ck_assert_uint_eq(g_stripes[i].tomb_count, 0u);
    pthread_mutex_unlock(&g_stripes[i].lock);
  }
  ck_assert_int_eq(ws_clients_touch(&b), hb);
  ck_assert_int_ge(ws_clients_touch(&a), 0);
}
END_TEST

START_TEST(remove_and_add_bytes_ignore_out_of_range_handles) {
  client_info_t info = make_info("10.0.0.1");
  int h;

  ws_clients_init(2);
  h = ws_clients_touch(&info);
  ck_assert_int_ge(h, 0);
  ws_clients_remove(1000);
  ws_clients_add_bytes(1000, 5);
  ck_assert_int_eq(ws_clients_touch(&info), h);
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
  tcase_add_test(tc, registry_without_capacity_rejects_everything);
  tcase_add_loop_test(tc, touch_separates_clients_that_differ_in_any_single_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_test(tc, persistent_clients_are_never_matched_by_touch);
  tcase_add_test(tc, persistent_registrations_fill_up_and_release_slots);
  tcase_add_test(tc, removed_slots_are_rehashed_and_survivors_still_resolve);
  tcase_add_test(tc, remove_and_add_bytes_ignore_out_of_range_handles);
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
