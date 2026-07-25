/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/fragment.h"
#include "lib/tva/tva_xml.h"

START_TEST(fragment_program_information_round_trips) {
  epg_programme_t pr, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char crid_out[EPG_ID_LEN * 3 + 64], expected_crid[EPG_ID_LEN * 3 + 64];

  memset(&pr, 0, sizeof pr);
  snprintf(pr.channel_id, sizeof pr.channel_id, "orf1");
  snprintf(pr.start, sizeof pr.start, "2020-12-15T12:30:45Z");
  snprintf(pr.title, sizeof pr.title, "News");
  snprintf(pr.desc, sizeof pr.desc, "Evening news");
  snprintf(pr.category, sizeof pr.category, "Current affairs");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_program_information(&pr, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);
  ck_assert_int_eq(fragment_decode_program_information(&br, &sr, crid_out, sizeof crid_out, &out), 0);

  tva_build_crid(pr.channel_id, pr.start, expected_crid, sizeof expected_crid);
  ck_assert_str_eq(crid_out, expected_crid);
  ck_assert_str_eq(out.title, "News");
  ck_assert_str_eq(out.desc, "Evening news");
  ck_assert_str_eq(out.category, "Current affairs");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_program_information_round_trips_empty_fields) {
  epg_programme_t pr, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char crid_out[EPG_ID_LEN * 3 + 64];

  memset(&pr, 0, sizeof pr);
  snprintf(pr.channel_id, sizeof pr.channel_id, "orf1");
  snprintf(pr.start, sizeof pr.start, "2020-12-15T12:30:45Z");
  /* title/desc/category all left empty */

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_program_information(&pr, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_program_information(&br, &sr, crid_out, sizeof crid_out, &out), 0);

  ck_assert_uint_eq(strlen(out.title), 0u);
  ck_assert_uint_eq(strlen(out.desc), 0u);
  ck_assert_uint_eq(strlen(out.category), 0u);

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_schedule_round_trips_and_filters_by_channel) {
  epg_programme_t all[3];
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  epg_doc_t doc;

  memset(all, 0, sizeof all);
  snprintf(all[0].channel_id, sizeof all[0].channel_id, "orf1");
  snprintf(all[0].start, sizeof all[0].start, "2020-12-15T12:00:00Z");
  snprintf(all[0].stop, sizeof all[0].stop, "2020-12-15T12:30:00Z");
  snprintf(all[1].channel_id, sizeof all[1].channel_id, "orf2"); /* different channel, must be filtered out */
  snprintf(all[1].start, sizeof all[1].start, "2020-12-15T12:00:00Z");
  snprintf(all[2].channel_id, sizeof all[2].channel_id, "orf1");
  snprintf(all[2].start, sizeof all[2].start, "2020-12-15T13:00:00Z");
  /* all[2].stop left empty: exercises the "no stop time" branch */

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_schedule("orf1", all, 3, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  epg_doc_init(&doc);
  ck_assert_int_eq(fragment_decode_schedule(&br, &sr, &doc, NULL, NULL), 0);

  ck_assert_int_eq(doc.programme_count, 2); /* only the two bbc1 entries */
  ck_assert_str_eq(doc.programmes[0].channel_id, "orf1");
  ck_assert_str_eq(doc.programmes[0].start, "2020-12-15T12:00:00Z");
  ck_assert_str_eq(doc.programmes[0].stop, "2020-12-15T12:30:00Z");
  ck_assert_str_eq(doc.programmes[1].start, "2020-12-15T13:00:00Z");
  ck_assert_uint_eq(strlen(doc.programmes[1].stop), 0u);

  epg_doc_free(&doc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static int fill_lookup(void *ctx, const char *crid, epg_programme_t *pr) {
  (void)ctx;
  (void)crid;
  snprintf(pr->title, sizeof pr->title, "looked up title");
  return 0;
}

START_TEST(fragment_schedule_decode_invokes_lookup) {
  epg_programme_t pr;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  epg_doc_t doc;

  memset(&pr, 0, sizeof pr);
  snprintf(pr.channel_id, sizeof pr.channel_id, "orf1");
  snprintf(pr.start, sizeof pr.start, "2020-12-15T12:00:00Z");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  fragment_encode_schedule("orf1", &pr, 1, &bw, &sw);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  epg_doc_init(&doc);
  ck_assert_int_eq(fragment_decode_schedule(&br, &sr, &doc, fill_lookup, NULL), 0);

  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].title, "looked up title");

  epg_doc_free(&doc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_service_information_round_trips) {
  epg_channel_t c, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;

  memset(&c, 0, sizeof c);
  snprintf(c.id, sizeof c.id, "orf1");
  snprintf(c.uri, sizeof c.uri, "rtp://239.1.1.1:5000");
  c.onid = 2;
  c.tsid = 1;
  c.sid = 101;
  epg_channel_add_name(&c, "ORFeins");
  epg_channel_add_name(&c, "ORF 1 HD");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_service_information(&c, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_service_information(&br, &sr, &out), 0);

  ck_assert_str_eq(out.id, "orf1");
  ck_assert_str_eq(out.uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(out.onid, 2u);
  ck_assert_uint_eq(out.tsid, 1u);
  ck_assert_uint_eq(out.sid, 101u);
  ck_assert_int_eq(out.name_count, 2);
  ck_assert_str_eq(out.names[0], "ORFeins");
  ck_assert_str_eq(out.names[1], "ORF 1 HD");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static Suite *fragment_suite(void) {
  Suite *s = suite_create("fragment");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fragment_program_information_round_trips);
  tcase_add_test(tc, fragment_program_information_round_trips_empty_fields);
  tcase_add_test(tc, fragment_schedule_round_trips_and_filters_by_channel);
  tcase_add_test(tc, fragment_schedule_decode_invokes_lookup);
  tcase_add_test(tc, fragment_service_information_round_trips);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fragment_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
