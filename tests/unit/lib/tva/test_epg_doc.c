/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/tva/epg_doc.h"

START_TEST(epg_add_channel_grows_past_initial_capacity) {
  epg_doc_t d;
  int i;
  epg_doc_init(&d);

  for (i = 0; i < 40; i++) { /* initial cap is 16, forces grow() at least twice */
    epg_channel_t *c = epg_add_channel(&d);
    ck_assert_ptr_nonnull(c);
    snprintf(c->id, sizeof c->id, "ch%d", i);
  }
  ck_assert_int_eq(d.channel_count, 40);
  for (i = 0; i < 40; i++) {
    char id[16];
    snprintf(id, sizeof id, "ch%d", i);
    ck_assert_str_eq(d.channels[i].id, id);
  }

  epg_doc_free(&d);
}
END_TEST

START_TEST(epg_find_channel_by_id) {
  epg_doc_t d;
  epg_channel_t *c;
  epg_doc_init(&d);
  c = epg_add_channel(&d);
  snprintf(c->id, sizeof c->id, "bbc1");
  c = epg_add_channel(&d);
  snprintf(c->id, sizeof c->id, "bbc2");

  ck_assert_ptr_nonnull(epg_find_channel(&d, "bbc1"));
  ck_assert_str_eq(epg_find_channel(&d, "bbc2")->id, "bbc2");
  ck_assert_ptr_null(epg_find_channel(&d, "nope"));

  epg_doc_free(&d);
}
END_TEST

START_TEST(epg_channel_add_name_caps_at_max_and_drops_silently) {
  epg_doc_t d;
  epg_channel_t *c;
  int i;
  epg_doc_init(&d);
  c = epg_add_channel(&d);

  for (i = 0; i < EPG_MAX_NAMES + 3; i++) {
    char name[16];
    snprintf(name, sizeof name, "name%d", i);
    epg_channel_add_name(c, name);
  }
  ck_assert_int_eq(c->name_count, EPG_MAX_NAMES);
  ck_assert_str_eq(c->names[0], "name0");
  ck_assert_str_eq(c->names[EPG_MAX_NAMES - 1], "name7");

  epg_doc_free(&d);
}
END_TEST

START_TEST(epg_add_programme_zeroes_new_entries) {
  epg_doc_t d;
  epg_programme_t *pr;
  epg_doc_init(&d);
  pr = epg_add_programme(&d);
  ck_assert_ptr_nonnull(pr);
  ck_assert_uint_eq(strlen(pr->title), 0u);
  ck_assert_uint_eq(strlen(pr->channel_id), 0u);
  ck_assert_int_eq(d.programme_count, 1);

  epg_doc_free(&d);
}
END_TEST

static Suite *epg_doc_suite(void) {
  Suite *s = suite_create("epg_doc");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, epg_add_channel_grows_past_initial_capacity);
  tcase_add_test(tc, epg_find_channel_by_id);
  tcase_add_test(tc, epg_channel_add_name_caps_at_max_and_drops_silently);
  tcase_add_test(tc, epg_add_programme_zeroes_new_entries);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(epg_doc_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
