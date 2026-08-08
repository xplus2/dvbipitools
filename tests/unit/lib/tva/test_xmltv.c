/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/tva/xmltv.h"

START_TEST(xmltv_write_read_round_trips) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "channel1");
  bcg_channel_add_name(c, "Channel One");
  bcg_channel_add_name(c, "Channel 1 HD");

  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "channel1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T12:30:45Z");
  snprintf(pr->stop, sizeof pr->stop, "2020-12-15T13:00:00Z");
  snprintf(pr->title, sizeof pr->title, "News & <Weather>");
  snprintf(pr->desc, sizeof pr->desc, "Today's \"top\" stories");
  snprintf(pr->category, sizeof pr->category, "News");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(xmltv_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 1);
  ck_assert_str_eq(doc2.channels[0].id, "channel1");
  ck_assert_int_eq(doc2.channels[0].name_count, 2);
  ck_assert_str_eq(doc2.channels[0].names[0], "Channel One");
  ck_assert_str_eq(doc2.channels[0].names[1], "Channel 1 HD");

  ck_assert_int_eq(doc2.programme_count, 1);
  ck_assert_str_eq(doc2.programmes[0].channel_id, "channel1");
  ck_assert_str_eq(doc2.programmes[0].start, "2020-12-15T12:30:45Z");
  ck_assert_str_eq(doc2.programmes[0].stop, "2020-12-15T13:00:00Z");
  ck_assert_str_eq(doc2.programmes[0].title, "News & <Weather>");
  ck_assert_str_eq(doc2.programmes[0].desc, "Today's \"top\" stories");
  ck_assert_str_eq(doc2.programmes[0].category, "News");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(xmltv_write_falls_back_to_id_when_no_names) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "channel1");

  f = tmpfile();
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(xmltv_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channels[0].name_count, 1);
  ck_assert_str_eq(doc2.channels[0].names[0], "channel1");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(xmltv_write_untitled_fallback_for_empty_title) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "channel1");
  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "channel1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T12:30:45Z");

  f = tmpfile();
  xmltv_write(f, &doc, "dvbipitools-test");
  rewind(f);

  bcg_doc_init(&doc2);
  xmltv_read(f, &doc2);
  fclose(f);

  ck_assert_str_eq(doc2.programmes[0].title, "(untitled)");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

static Suite *xmltv_suite(void) {
  Suite *s = suite_create("xmltv");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xmltv_write_read_round_trips);
  tcase_add_test(tc, xmltv_write_falls_back_to_id_when_no_names);
  tcase_add_test(tc, xmltv_write_untitled_fallback_for_empty_title);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(xmltv_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
