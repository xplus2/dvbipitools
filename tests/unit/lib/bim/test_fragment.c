/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/fragment.h"
#include "lib/tva/tva_xml.h"
#include "lib/sys/ioutil.h"

START_TEST(fragment_program_information_round_trips) {
  bcg_programme_t pr, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char crid_out[BCG_ID_LEN * 3 + 64], expected_crid[BCG_ID_LEN * 3 + 64];

  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "channel1");
  bufcpy(pr.start, sizeof pr.start, "2020-12-15T12:30:45Z");
  bcg_set_text(&pr.title, "News");
  bcg_set_text(&pr.desc, "Evening news");
  bcg_set_text(&pr.category, "Current affairs");

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

  bcg_programme_free_text(&pr);
  bcg_programme_free_text(&out);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_program_information_round_trips_empty_fields) {
  bcg_programme_t pr, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char crid_out[BCG_ID_LEN * 3 + 64];

  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "channel1");
  bufcpy(pr.start, sizeof pr.start, "2020-12-15T12:30:45Z");
  /* title/desc/category all left empty */

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_program_information(&pr, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_program_information(&br, &sr, crid_out, sizeof crid_out, &out), 0);

  ck_assert_ptr_null(out.title);
  ck_assert_ptr_null(out.desc);
  ck_assert_ptr_null(out.category);

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_schedule_round_trips_and_filters_by_channel) {
  bcg_programme_t all[3];
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  bcg_doc_t doc;

  memset(all, 0, sizeof all);
  bufcpy(all[0].channel_id, sizeof all[0].channel_id, "channel1");
  bufcpy(all[0].start, sizeof all[0].start, "2020-12-15T12:00:00Z");
  bufcpy(all[0].stop, sizeof all[0].stop, "2020-12-15T12:30:00Z");
  bufcpy(all[1].channel_id, sizeof all[1].channel_id, "channel2"); /* different channel, must be filtered out */
  bufcpy(all[1].start, sizeof all[1].start, "2020-12-15T12:00:00Z");
  bufcpy(all[2].channel_id, sizeof all[2].channel_id, "channel1");
  bufcpy(all[2].start, sizeof all[2].start, "2020-12-15T13:00:00Z");
  /* all[2].stop left empty: exercises the "no stop time" branch */

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_schedule("channel1", all, 0, (const int[]){2, -1, -1}, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  bcg_doc_init(&doc);
  ck_assert_int_eq(fragment_decode_schedule(&br, &sr, &doc, NULL, NULL), 0);

  ck_assert_int_eq(doc.programme_count, 2); /* only the two bbc1 entries */
  ck_assert_str_eq(doc.programmes[0].channel_id, "channel1");
  ck_assert_str_eq(doc.programmes[0].start, "2020-12-15T12:00:00Z");
  ck_assert_str_eq(doc.programmes[0].stop, "2020-12-15T12:30:00Z");
  ck_assert_str_eq(doc.programmes[1].start, "2020-12-15T13:00:00Z");
  ck_assert_uint_eq(strlen(doc.programmes[1].stop), 0u);

  bcg_doc_free(&doc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static int fill_lookup(void *ctx, const char *crid, bcg_programme_t *pr) {
  (void)ctx;
  (void)crid;
  bcg_set_text(&pr->title, "looked up title");
  return 0;
}

START_TEST(fragment_schedule_decode_invokes_lookup) {
  bcg_programme_t pr;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  bcg_doc_t doc;

  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "channel1");
  bufcpy(pr.start, sizeof pr.start, "2020-12-15T12:00:00Z");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  fragment_encode_schedule("channel1", &pr, 0, (const int[]){-1}, &bw, &sw);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  bcg_doc_init(&doc);
  ck_assert_int_eq(fragment_decode_schedule(&br, &sr, &doc, fill_lookup, NULL), 0);

  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].title, "looked up title");

  bcg_doc_free(&doc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_service_information_round_trips) {
  bcg_channel_t c, out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;

  memset(&c, 0, sizeof c);
  bufcpy(c.id, sizeof c.id, "channel1");
  bufcpy(c.uri, sizeof c.uri, "rtp://239.1.1.1:5000");
  c.onid = 2;
  c.tsid = 1;
  c.sid = 0xabc;
  bcg_channel_add_name(&c, "Channel One");
  bcg_channel_add_name(&c, "Channel 1 HD");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_service_information(&c, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_service_information(&br, &sr, &out), 0);

  ck_assert_str_eq(out.id, "channel1");
  ck_assert_str_eq(out.uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(out.onid, 2u);
  ck_assert_uint_eq(out.tsid, 1u);
  ck_assert_uint_eq(out.sid, 0xabcu);
  ck_assert_int_eq(out.name_count, 2);
  ck_assert_str_eq(out.names[0], "Channel One");
  ck_assert_str_eq(out.names[1], "Channel 1 HD");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_schedule_wire_layout_follows_ts102323_annex_b) {
  bcg_programme_t pr;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *b;
  const unsigned char *str;
  size_t blen;
  size_t slen;

  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "s");
  bufcpy(pr.start, sizeof pr.start, "2030-01-01T12:00:00Z");
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_schedule("s", &pr, 0, (const int[]){-1}, &bw, &sw), 0);
  b = bitwriter_data(&bw, &blen);
  str = strrepo_writer_data(&sw, &slen);
  ck_assert_uint_eq(blen, 12);
  ck_assert_uint_eq(b[0], 0x0F); /* DecodingModes */
  ck_assert_uint_eq(b[1], 0x00); /* end, fragmentId, fragmentVersion, start absent, then 0000 of count */
  ck_assert_uint_eq(b[2], 0x84); /* count 1 (vluimsbf5 00001), crid string flag, 3 absent, start present, dateTime flag 00 */
  ck_assert_uint_eq(b[11], 0x00);
  ck_assert_uint_gt(slen, 1);
  ck_assert_str_eq((const char *)str + 1, "s"); /* serviceIDRef first, crid second */

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_service_information_without_names_round_trips) {
  bcg_channel_t c;
  bcg_channel_t out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata;
  const unsigned char *swdata;
  size_t bwlen;
  size_t swlen;

  memset(&c, 0, sizeof c);
  bufcpy(c.id, sizeof c.id, "channel1");
  bufcpy(c.uri, sizeof c.uri, "rtp://239.1.1.1:5000");
  c.onid = 2;
  c.tsid = 1;
  c.sid = 7;

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_service_information(&c, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_service_information(&br, &sr, &out), 0);
  ck_assert_int_eq(out.name_count, 0); /* schema needs a Name, the stand-in is not returned */
  ck_assert_uint_eq(out.sid, 7u);

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_service_information_without_triplet_stays_zero) {
  bcg_channel_t c;
  bcg_channel_t out;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata;
  const unsigned char *swdata;
  size_t bwlen;
  size_t swlen;

  memset(&c, 0, sizeof c);
  bufcpy(c.id, sizeof c.id, "channel1");
  bufcpy(c.uri, sizeof c.uri, "rtp://239.1.1.1:5000");

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(fragment_encode_service_information(&c, &bw, &sw), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);

  bitreader_init(&br, bwdata, bwlen);
  strrepo_reader_init(&sr, swdata, swlen);
  ck_assert_int_eq(fragment_decode_service_information(&br, &sr, &out), 0);
  ck_assert_uint_eq(out.onid | out.tsid | out.sid, 0u);

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_payload_decodes_published_time_optimized_locator_and_duration) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *str;
  size_t blen;
  size_t slen;
  bim_node_t *root;
  const bim_node_t *ev;
  unsigned mjd = (unsigned)date_to_mjd(2030, 1, 1) & 0xFFFFu;

  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "svc");
  strrepo_writer_put(&sw, "crid://x/1");
  strrepo_writer_put(&sw, "T");
  bitwriter_put(&bw, 0x0F, 8);
  bitwriter_put(&bw, 0, 1); /* end */
  bitwriter_put(&bw, 0, 2); /* fragmentId, fragmentVersion */
  bitwriter_put(&bw, 1, 1); /* start */
  bitwriter_put(&bw, 1, 2); /* PublishedTime */
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, mjd, 16);
  bitwriter_put(&bw, 720, 11);
  bitwriter_put_vluimsbf4(&bw, 1); /* ScheduleEvent */
  bitwriter_put(&bw, 0, 1); /* crid: string */
  bitwriter_put(&bw, 1, 1); /* ProgramURL */
  bitwriter_put(&bw, 1, 1); /* optimized */
  bitwriter_put(&bw, 1, 1); /* prefix */
  bitwriter_put(&bw, 0x0011, 16); /* tsid */
  bitwriter_put(&bw, 0x0022, 16); /* onid */
  bitwriter_put(&bw, 0x0033, 16); /* sid */
  bitwriter_put(&bw, 0, 1); /* ctag */
  bitwriter_put(&bw, 0, 1); /* cid */
  bitwriter_put(&bw, 1, 2); /* event_id */
  bitwriter_put(&bw, 0x1001, 16);
  bitwriter_put(&bw, 0, 1); /* time */
  bitwriter_put(&bw, 0, 1); /* path */
  bitwriter_put(&bw, 0, 1); /* InstanceMetadataId */
  bitwriter_put(&bw, 1, 1); /* InstanceDescription */
  bitwriter_put(&bw, 1, 1); /* Title */
  bitwriter_put(&bw, 0, 2); /* Title type, xml:lang */
  bitwriter_put(&bw, 0, 1); /* Synopsis */
  bitwriter_put_vluimsbf4(&bw, 0); /* Genre */
  bitwriter_put(&bw, 0, 1); /* PurchaseList */
  bitwriter_put(&bw, 0, 1); /* AVAttributes */
  bitwriter_put_vluimsbf4(&bw, 0); /* MemberOf */
  bitwriter_put(&bw, 0, 1); /* PurchaseList */
  bitwriter_put(&bw, 1, 1); /* PublishedStartTime */
  bitwriter_put(&bw, 1, 2);
  bitwriter_put(&bw, 0, 1); /* date reused */
  bitwriter_put(&bw, 750, 11);
  bitwriter_put(&bw, 0, 1); /* PublishedEndTime */
  bitwriter_put(&bw, 1, 1); /* PublishedDuration */
  bitwriter_put(&bw, 1, 1);
  bitwriter_put(&bw, 60, 11);
  bitwriter_put(&bw, 0, 5); /* Live .. Free */
  b = bitwriter_data(&bw, &blen);
  str = strrepo_writer_data(&sw, &slen);

  bitreader_init(&br, b, blen);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, slen), 0);
  ck_assert_int_eq(bim_decode_payload(tva_type_schedule, "Schedule", &br, &sr, &root), 0);
  ck_assert_str_eq(bim_node_attr(root, "serviceIDRef"), "svc");
  ck_assert_str_eq(bim_node_attr(root, "start"), "2030-01-01T12:00:00Z");
  ck_assert_int_eq(root->nkids, 1);
  ev = root->kids[0];
  ck_assert_str_eq(bim_node_attr(bim_node_child(ev, "Program"), "crid"), "crid://x/1");
  ck_assert_str_eq(bim_node_child(ev, "ProgramURL")->text, "dvb://22.11.33;1001");
  ck_assert_str_eq(bim_node_child(bim_node_child(ev, "InstanceDescription"), "Title")->text, "T");
  ck_assert_str_eq(bim_node_child(ev, "PublishedStartTime")->text, "2030-01-01T12:30:00Z");
  ck_assert_str_eq(bim_node_child(ev, "PublishedDuration")->text, "PT1H0M");

  bim_node_free(root);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(fragment_payload_decode_rejects_type_casting_and_length_modes) {
  static const unsigned char bad_modes[] = {0x1F, 0x00, 0x00};
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char repo[] = {0x01, 's', 0};
  const unsigned char first_byte[3] = {0x4F, 0x2F, 0x1F};
  bim_node_t *root = NULL;
  for (int m = 0; m < 3; m++) {
    unsigned char buf[3];
    memcpy(buf, bad_modes, sizeof buf);
    buf[0] = first_byte[m];
    bitreader_init(&br, buf, sizeof buf);
    strrepo_reader_init(&sr, repo, sizeof repo);
    ck_assert_int_eq(bim_decode_payload(tva_type_schedule, "Schedule", &br, &sr, &root), -1);
  }
  ck_assert_ptr_null(root);
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
  tcase_add_test(tc, fragment_service_information_without_triplet_stays_zero);
  tcase_add_test(tc, fragment_service_information_without_names_round_trips);
  tcase_add_test(tc, fragment_schedule_wire_layout_follows_ts102323_annex_b);
  tcase_add_test(tc, fragment_payload_decodes_published_time_optimized_locator_and_duration);
  tcase_add_test(tc, fragment_payload_decode_rejects_type_casting_and_length_modes);
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
