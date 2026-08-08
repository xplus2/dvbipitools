/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/tva/tva_xml.h"

START_TEST(tva_xml_write_read_round_trips) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "bbc1");
  snprintf(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  bcg_channel_add_name(c, "BBC One");

  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "bbc1");
  snprintf(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  snprintf(pr->stop, sizeof pr->stop, "2024-03-15T13:00:00Z");
  snprintf(pr->title, sizeof pr->title, "News & <Weather>");
  snprintf(pr->desc, sizeof pr->desc, "Today's \"top\" stories");
  snprintf(pr->category, sizeof pr->category, "News");

  f = tmpfile();
  ck_assert_ptr_nonnull(f);
  tva_xml_write(f, &doc);
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 1);
  ck_assert_str_eq(doc2.channels[0].id, "bbc1");
  ck_assert_str_eq(doc2.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc2.channels[0].onid, 2u);
  ck_assert_uint_eq(doc2.channels[0].tsid, 1u);
  ck_assert_uint_eq(doc2.channels[0].sid, 101u);
  ck_assert_int_eq(doc2.channels[0].name_count, 1);
  ck_assert_str_eq(doc2.channels[0].names[0], "BBC One");

  ck_assert_int_eq(doc2.programme_count, 1);
  ck_assert_str_eq(doc2.programmes[0].channel_id, "bbc1");
  ck_assert_str_eq(doc2.programmes[0].start, "2024-03-15T12:30:45Z");
  ck_assert_str_eq(doc2.programmes[0].stop, "2024-03-15T13:00:00Z");
  ck_assert_str_eq(doc2.programmes[0].title, "News & <Weather>");
  ck_assert_str_eq(doc2.programmes[0].desc, "Today's \"top\" stories");
  ck_assert_str_eq(doc2.programmes[0].category, "News");

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(tva_xml_write_drops_channels_without_uri) {
  bcg_doc_t doc, doc2;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  FILE *f;

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc); /* no uri set */
  snprintf(c->id, sizeof c->id, "noturi");
  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "noturi");
  snprintf(pr->start, sizeof pr->start, "2024-03-15T12:30:45Z");
  snprintf(pr->title, sizeof pr->title, "Should be dropped");

  f = tmpfile();
  tva_xml_write(f, &doc);
  rewind(f);

  bcg_doc_init(&doc2);
  ck_assert_int_eq(tva_xml_read(f, &doc2), 0);
  fclose(f);

  ck_assert_int_eq(doc2.channel_count, 0);
  ck_assert_int_eq(doc2.programme_count, 0);

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
}
END_TEST

START_TEST(tva_build_crid_percent_encodes_and_compacts_time) {
  char out[256];
  tva_build_crid("bbc/one", "2024-03-15T12:30:45Z", out, sizeof out);
  ck_assert_str_eq(out, "crid://dipixmltv.invalid/bbc%2Fone/20240315123045");
}
END_TEST

static Suite *tva_xml_suite(void) {
  Suite *s = suite_create("tva_xml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tva_xml_write_read_round_trips);
  tcase_add_test(tc, tva_xml_write_drops_channels_without_uri);
  tcase_add_test(tc, tva_build_crid_percent_encodes_and_compacts_time);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tva_xml_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
