/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/accessunit.h"

static bcg_doc_t *build_doc(void) {
  static bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;

  bcg_doc_init(&doc);

  c = bcg_add_channel(&doc);
  snprintf(c->id, sizeof c->id, "channel1");
  snprintf(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->onid = 2;
  c->tsid = 1;
  c->sid = 101;
  bcg_channel_add_name(c, "Channel One");

  c = bcg_add_channel(&doc); /* no uri: must be excluded entirely */
  snprintf(c->id, sizeof c->id, "nouri");

  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "channel1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T12:00:00Z");
  snprintf(pr->stop, sizeof pr->stop, "2020-12-15T12:30:00Z");
  snprintf(pr->title, sizeof pr->title, "News");
  snprintf(pr->desc, sizeof pr->desc, "Evening news");
  snprintf(pr->category, sizeof pr->category, "Current affairs");

  pr = bcg_add_programme(&doc);
  snprintf(pr->channel_id, sizeof pr->channel_id, "channel1");
  snprintf(pr->start, sizeof pr->start, "2020-12-15T13:00:00Z");
  snprintf(pr->title, sizeof pr->title, "Weather");

  return &doc;
}

START_TEST(accessunit_encode_decode_round_trips_full_doc) {
  bcg_doc_t *doc = build_doc();
  bcg_doc_t doc2;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  int nfuu, nfuu2;

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(accessunit_encode(doc, &bw, &sw, &nfuu), 0);
  /* 2 program-information + 1 schedule + 1 service-information: nouri channel excluded */
  ck_assert_int_eq(nfuu, 4);

  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);
  bcg_doc_init(&doc2);
  ck_assert_int_eq(accessunit_decode(&br, &sr, &doc2, &nfuu2), 0);

  ck_assert_int_eq(nfuu2, 4);
  ck_assert_int_eq(doc2.channel_count, 1); /* nouri channel never round-trips */
  ck_assert_str_eq(doc2.channels[0].id, "channel1");
  ck_assert_str_eq(doc2.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc2.channels[0].onid, 2u);
  ck_assert_uint_eq(doc2.channels[0].tsid, 1u);
  ck_assert_uint_eq(doc2.channels[0].sid, 101u);
  ck_assert_int_eq(doc2.channels[0].name_count, 1);
  ck_assert_str_eq(doc2.channels[0].names[0], "Channel One");

  ck_assert_int_eq(doc2.programme_count, 2);
  ck_assert_str_eq(doc2.programmes[0].channel_id, "channel1");
  ck_assert_str_eq(doc2.programmes[0].start, "2020-12-15T12:00:00Z");
  ck_assert_str_eq(doc2.programmes[0].stop, "2020-12-15T12:30:00Z");
  ck_assert_str_eq(doc2.programmes[0].title, "News");
  ck_assert_str_eq(doc2.programmes[0].desc, "Evening news");
  ck_assert_str_eq(doc2.programmes[0].category, "Current affairs");
  ck_assert_str_eq(doc2.programmes[1].start, "2020-12-15T13:00:00Z");
  ck_assert_str_eq(doc2.programmes[1].title, "Weather");

  bcg_doc_free(doc);
  bcg_doc_free(&doc2);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(accessunit_encode_empty_doc_has_zero_fuus) {
  bcg_doc_t doc, doc2;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  int nfuu, nfuu2;

  bcg_doc_init(&doc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(accessunit_encode(&doc, &bw, &sw, &nfuu), 0);
  ck_assert_int_eq(nfuu, 0);

  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  bcg_doc_init(&doc2);
  ck_assert_int_eq(accessunit_decode(&br, &sr, &doc2, &nfuu2), 0);
  ck_assert_int_eq(nfuu2, 0);
  ck_assert_int_eq(doc2.channel_count, 0);
  ck_assert_int_eq(doc2.programme_count, 0);

  bcg_doc_free(&doc);
  bcg_doc_free(&doc2);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static Suite *accessunit_suite(void) {
  Suite *s = suite_create("accessunit");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, accessunit_encode_decode_round_trips_full_doc);
  tcase_add_test(tc, accessunit_encode_empty_doc_has_zero_fuus);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(accessunit_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
