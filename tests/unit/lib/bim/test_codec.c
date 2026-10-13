/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec.h"

START_TEST(dvb_string_round_trips) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[64];
  strrepo_writer_init(&sw);

  ck_assert_int_eq(dvb_string_encode(&sw, "wurgl"), 0);
  data = strrepo_writer_data(&sw, &len);
  ck_assert_int_eq(strrepo_reader_init(&sr, data, len), 0);
  ck_assert_int_eq(dvb_string_decode(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "wurgl");

  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(dvb_locator_round_trips_string_fallback) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char out[128];
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);

  ck_assert_int_eq(dvb_locator_encode(&bw, &sw, "http://example.com/stream"), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);

  ck_assert_int_eq(dvb_locator_decode(&br, &sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "http://example.com/stream");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(dvb_datetime_round_trips_utc) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  char out[32];
  bitwriter_init(&bw);

  ck_assert_int_eq(dvb_datetime_encode(&bw, "2020-12-15T12:45:42Z"), 0);
  data = bitwriter_data(&bw, &len);
  bitreader_init(&br, data, len);
  ck_assert_int_eq(dvb_datetime_decode(&br, out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:45:42Z");

  bitwriter_free(&bw);
}
END_TEST

START_TEST(dvb_datetime_normalizes_offset_to_utc_across_midnight) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  char out[32];
  bitwriter_init(&bw);

  /* 00:30 local at +02:00 is 22:30 UTC the previous day */
  ck_assert_int_eq(dvb_datetime_encode(&bw, "2020-12-15T00:30:00+02:00"), 0);
  data = bitwriter_data(&bw, &len);
  bitreader_init(&br, data, len);
  ck_assert_int_eq(dvb_datetime_decode(&br, out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-14T22:30:00Z");

  bitwriter_free(&bw);
}
END_TEST

START_TEST(dvb_datetime_encode_rejects_malformed_input) {
  bitwriter_t bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(dvb_datetime_encode(&bw, "not-a-date"), -1);
  bitwriter_free(&bw);
}
END_TEST

static int decode_raw_datetime(uint64_t mjd, uint64_t ms) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  char out[32];
  int rc;
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 2), 0);
  ck_assert_int_eq(bitwriter_put(&bw, mjd, 32), 0);
  ck_assert_int_eq(bitwriter_put(&bw, ms, 32), 0);
  data = bitwriter_data(&bw, &len);
  bitreader_init(&br, data, len);
  rc = dvb_datetime_decode(&br, out, sizeof out);
  bitwriter_free(&bw);
  return rc;
}

START_TEST(dvb_datetime_decode_rejects_ms_out_of_day) {
  ck_assert_int_eq(decode_raw_datetime(59198, 86399000ULL), 0);
  ck_assert_int_eq(decode_raw_datetime(59198, 86400000ULL), -1);
  ck_assert_int_eq(decode_raw_datetime(59198, 0xFFFFFFFFULL), -1);
}
END_TEST

START_TEST(dvb_datetime_decode_rejects_mjd_out_of_range) {
  ck_assert_int_eq(decode_raw_datetime(15078, 0), -1);
  ck_assert_int_eq(decode_raw_datetime(15079, 0), 0);
  ck_assert_int_eq(decode_raw_datetime(88127, 0), 0);
  ck_assert_int_eq(decode_raw_datetime(88128, 0), -1);
  ck_assert_int_eq(decode_raw_datetime(0xFFFFFFFFULL, 0), -1);
}
END_TEST

START_TEST(dvb_datetime_encode_rejects_date_out_of_range) {
  bitwriter_t bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(dvb_datetime_encode(&bw, "1900-02-28T00:00:00Z"), -1);
  ck_assert_int_eq(dvb_datetime_encode(&bw, "2100-03-01T00:00:00Z"), -1);
  bitwriter_free(&bw);
}
END_TEST

START_TEST(dvb_controlledterm_round_trips_dotted_term) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char out[128];
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);

  ck_assert_int_eq(dvb_controlledterm_encode(&bw, &sw, "urn:tva:metadata:cs:ContentCS:2011:3.1.1"), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  ck_assert_uint_eq(bwlen, 1u); /* encoding_flag only */
  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);

  ck_assert_int_eq(dvb_controlledterm_decode(&br, &sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "urn:tva:metadata:cs:ContentCS:2011:3.1.1");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(dvb_controlledterm_round_trips_unknown_scheme_as_string) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata, *swdata;
  size_t bwlen, swlen;
  char out[128];
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);

  ck_assert_int_eq(dvb_controlledterm_encode(&bw, &sw, "urn:example:custom:Foo"), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);

  ck_assert_int_eq(dvb_controlledterm_decode(&br, &sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "urn:example:custom:Foo");

  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(dvb_controlledterm_decode_rejects_compact_form) {
  bitreader_t br;
  strrepo_reader_t sr;
  strrepo_writer_t sw;
  const unsigned char bits[] = {0x80};
  const unsigned char *swdata;
  size_t swlen;
  char out[64];
  strrepo_writer_init(&sw);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bits, sizeof bits);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);
  ck_assert_int_eq(dvb_controlledterm_decode(&br, &sr, out, sizeof out), -1);
  strrepo_writer_free(&sw);
}
END_TEST

static Suite *codec_suite(void) {
  Suite *s = suite_create("bim_codec");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, dvb_string_round_trips);
  tcase_add_test(tc, dvb_locator_round_trips_string_fallback);
  tcase_add_test(tc, dvb_datetime_round_trips_utc);
  tcase_add_test(tc, dvb_datetime_normalizes_offset_to_utc_across_midnight);
  tcase_add_test(tc, dvb_datetime_encode_rejects_malformed_input);
  tcase_add_test(tc, dvb_datetime_decode_rejects_ms_out_of_day);
  tcase_add_test(tc, dvb_datetime_decode_rejects_mjd_out_of_range);
  tcase_add_test(tc, dvb_datetime_encode_rejects_date_out_of_range);
  tcase_add_test(tc, dvb_controlledterm_round_trips_dotted_term);
  tcase_add_test(tc, dvb_controlledterm_round_trips_unknown_scheme_as_string);
  tcase_add_test(tc, dvb_controlledterm_decode_rejects_compact_form);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(codec_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
