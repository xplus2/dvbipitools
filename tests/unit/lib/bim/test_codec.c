/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
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

START_TEST(dvb_locator_decode_rejects_value_longer_than_buffer) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bwdata;
  const unsigned char *swdata;
  size_t bwlen;
  size_t swlen;
  char uri[300];
  char out[128];
  memset(uri, 'a', sizeof uri - 1);
  uri[sizeof uri - 1] = '\0';
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);

  ck_assert_int_eq(dvb_locator_encode(&bw, &sw, uri), 0);
  bwdata = bitwriter_data(&bw, &bwlen);
  swdata = strrepo_writer_data(&sw, &swlen);
  bitreader_init(&br, bwdata, bwlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, swdata, swlen), 0);
  ck_assert_int_eq(dvb_locator_decode(&br, &sr, out, sizeof out), -1);

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

START_TEST(dvb_datetime_no_offset_is_utc) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  char out[32];
  bitwriter_init(&bw);

  ck_assert_int_eq(dvb_datetime_encode(&bw, "2020-12-15T12:45:42"), 0);
  data = bitwriter_data(&bw, &len);
  bitreader_init(&br, data, len);
  ck_assert_int_eq(dvb_datetime_decode(&br, out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:45:42Z");

  bitwriter_free(&bw);
}
END_TEST

/* TS 102 323 9.4.3.5: PublishedTime with date reuse, 64 bit form when seconds set */
START_TEST(dvb_datetime_published_reuses_date_and_keeps_seconds) {
  static const char *const in[] = {"2030-01-01T12:00:00Z", "2030-01-01T13:00:00Z", "2030-01-01T13:00:05Z", "2030-01-02T00:00:00Z"};
  static const int flag[] = {1, 1, 0, 1};
  static const int date_flag[] = {1, 0, -1, 1};
  dvb_codec_state_t enc;
  dvb_codec_state_t dec;
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *b;
  size_t blen;
  uint64_t v;
  char out[64];
  memset(&enc, 0, sizeof enc);
  memset(&dec, 0, sizeof dec);
  bitwriter_init(&bw);
  for (int i = 0; i < 4; i++) ck_assert_int_eq(dvb_datetime_encode_st(&bw, &enc, in[i], 1), 0);
  b = bitwriter_data(&bw, &blen);
  bitreader_init(&br, b, blen);
  for (int i = 0; i < 4; i++) {
    bitreader_t peek = br;
    ck_assert_int_eq(bitreader_get(&peek, 2, &v), 0);
    ck_assert_uint_eq(v, (uint64_t)flag[i]);
    if (date_flag[i] >= 0) {
      ck_assert_int_eq(bitreader_get(&peek, 1, &v), 0);
      ck_assert_uint_eq(v, (uint64_t)date_flag[i]);
    }
    ck_assert_int_eq(dvb_datetime_decode_st(&br, &dec, out, sizeof out), 0);
    ck_assert_str_eq(out, in[i]);
  }
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

START_TEST(dvb_datetime_encode_rejects_invalid_fields) {
  static const char *const bad[] = {
    "2020-12-15T23:59:60Z", "2020-12-15T23:59:99Z", "2020-12-15T24:00:00Z", "2020-12-15T12:60:00Z",
    "2020-13-15T00:00:00Z", "2020-00-15T00:00:00Z", "2020-12-45T00:00:00Z", "2020-12-00T00:00:00Z",
    "2021-02-29T00:00:00Z", "2020-04-31T00:00:00Z", "2020-12-15T00:00:00+24:00", "2020-12-15T00:00:00+02:60",
    "2020-12-15T12:00:00Zjunk", "2020-12-15T12:00:00ZZ", "2020-12-15T12:00:00+02:00x", "2020-12-15T12:00:00.5Z ",
  };
  bitwriter_t bw;
  bitwriter_init(&bw);
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_int_eq(dvb_datetime_encode(&bw, bad[i]), -1);
  ck_assert_int_eq(dvb_datetime_encode(&bw, "2020-02-29T23:59:59Z"), 0);
  bitwriter_free(&bw);
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

static int encode_type(const bim_type_t *type, const char *text) {
  bim_node_t *n = bim_node_new("X");
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc;
  ck_assert_ptr_nonnull(n);
  ck_assert_int_eq(bim_node_set_text(n, text), 0);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  rc = bim_encode_payload(type, n, 0, &bw, &sw);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bim_node_free(n);
  return rc;
}

static int encode_simple(const bim_simple_t *s, const char *text) {
  const bim_type_t type = {.content = BIM_CONTENT_SIMPLE, .simple = s};
  return encode_type(&type, text);
}

static const bim_simple_t s_ulong = {.codec = BIM_RANGE, .range = {0, UINT64_MAX}};
static const bim_simple_t s_ushort = {.codec = BIM_RANGE, .range = {0, 65535}};
static const bim_simple_t s_ubyte = {.codec = BIM_RANGE, .range = {0, 255}};
static const bim_simple_t s_int = {.codec = BIM_RANGE, .range = {INT32_MIN, UINT32_MAX}};
static const bim_simple_t s_svlu5 = {.codec = BIM_SVLU5};
static const bim_simple_t s_float = {.codec = BIM_FLOAT};
static const bim_simple_t s_double = {.codec = BIM_DOUBLE};
static const bim_simple_t s_bool = {.codec = BIM_BOOLEAN};
static const bim_simple_t s_string = {.codec = BIM_STRING};
static const bim_simple_t s_utf8 = {.codec = BIM_UTF8};

/* encodes text with s, checks bits after DecodingModes, decodes text back as want */
static void check_simple(const bim_simple_t *s, const char *text, const char *bits, const char *want) {
  const bim_type_t type = {.content = BIM_CONTENT_SIMPLE, .simple = s};
  bim_node_t *n = bim_node_new("X");
  bim_node_t *got = NULL;
  bitwriter_t bw;
  bitwriter_t exp;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *w;
  const unsigned char *str;
  size_t blen;
  size_t wlen;
  size_t slen;
  ck_assert_ptr_nonnull(n);
  ck_assert_int_eq(bim_node_set_text(n, text), 0);
  bitwriter_init(&bw);
  bitwriter_init(&exp);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bim_encode_payload(&type, n, 0, &bw, &sw), 0);
  bitwriter_put(&exp, 0x0F, 8);
  for (const char *p = bits; *p; p++)
    if (*p != ' ') bitwriter_put(&exp, *p == '1', 1);
  b = bitwriter_data(&bw, &blen);
  w = bitwriter_data(&exp, &wlen);
  ck_assert_uint_eq(blen, wlen);
  ck_assert_mem_eq(b, w, wlen);
  str = strrepo_writer_data(&sw, &slen);
  bitreader_init(&br, w, wlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, slen), 0);
  ck_assert_int_eq(bim_decode_payload(&type, "X", NULL, &br, &sr, &got), 0);
  ck_assert_str_eq(got->text, want ? want : text);
  bim_node_free(got);
  bim_node_free(n);
  bitwriter_free(&bw);
  bitwriter_free(&exp);
  strrepo_writer_free(&sw);
}

START_TEST(vluimsbf5_range_accepts_plain_and_padded_numbers) {
  ck_assert_int_eq(encode_simple(&s_ulong, "0"), 0);
  ck_assert_int_eq(encode_simple(&s_ulong, "12345"), 0);
  ck_assert_int_eq(encode_simple(&s_ulong, " 7\n"), 0);
  ck_assert_int_eq(encode_simple(&s_ulong, "18446744073709551615"), 0);
  check_simple(&s_ulong, "+7", "00111", "7");
}
END_TEST

START_TEST(vluimsbf5_range_rejects_garbage) {
  const char *bad[] = {"", " ", "abc", "-1", "++1", "12x", "1 2", "0x10", "18446744073709551616"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_int_eq(encode_simple(&s_ulong, bad[i]), -1);
}
END_TEST

START_TEST(svlu5_and_fixed_ranges_encode_bounds) {
  const char *bad[] = {"", "-", "--1", "+-1", "1-"};
  ck_assert_int_eq(encode_simple(&s_svlu5, "-12"), 0);
  ck_assert_int_eq(encode_simple(&s_svlu5, " 0 "), 0);
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_int_eq(encode_simple(&s_svlu5, bad[i]), -1);
  ck_assert_int_eq(encode_simple(&s_ushort, "65535"), 0);
  ck_assert_int_eq(encode_simple(&s_ushort, "65536"), -1);
  ck_assert_int_eq(encode_simple(&s_ushort, "-1"), -1);
  ck_assert_int_eq(encode_simple(&s_ubyte, "256"), -1);
  ck_assert_int_eq(encode_simple(&s_int, "2147483647"), 0);
  ck_assert_int_eq(encode_simple(&s_int, "2147483648"), -1);
  ck_assert_int_eq(encode_simple(&s_int, "-2147483649"), -1);
  ck_assert_int_eq(encode_simple(&s_double, "1e3"), 0);
  ck_assert_int_eq(encode_simple(&s_double, "x"), -1);
  ck_assert_int_eq(encode_simple(&s_float, "1.0 2"), -1);
  ck_assert_int_eq(encode_simple(&s_double, "inf"), -1);
  ck_assert_int_eq(encode_simple(&s_double, "0x10"), -1);
  ck_assert_int_eq(encode_simple(&s_double, "1e"), -1);
  check_simple(&s_double, "-INF", "1111111111110000000000000000000000000000000000000000000000000000", NULL);
  check_simple(&s_float, "NaN", "01111111110000000000000000000000", NULL);
  check_simple(&s_float, ".5", "00111111000000000000000000000000", "0.5");
}
END_TEST

START_TEST(range_encode_rejects_out_of_bounds) {
  const bim_simple_t r = {.codec = BIM_RANGE, .range = {-100, 200}};
  const char *bad[] = {"101", "-101", "", "x", "9223372036854775808"};
  ck_assert_int_eq(encode_simple(&r, "-100"), 0);
  ck_assert_int_eq(encode_simple(&r, "100"), 0);
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_int_eq(encode_simple(&r, bad[i]), -1);
}
END_TEST

/* ISO 23001-1 7.5.4.1, 7.5.4.2.1 */
START_TEST(integer_codings_follow_iso_bim) {
  static const bim_simple_t r1 = {.codec = BIM_RANGE, .range = {-100, 200}};
  static const bim_simple_t r2 = {.codec = BIM_RANGE, .range = {1, 14}};
  static const bim_simple_t r3 = {.codec = BIM_RANGE, .range = {0, 360}};
  static const bim_simple_t r4 = {.codec = BIM_RANGE, .range = {-128, 255}};
  static const bim_simple_t r5 = {.codec = BIM_RANGE, .range = {0, 100000}};
  check_simple(&s_ulong, "5", "00101", NULL);
  check_simple(&s_ulong, "300", "110 000100101100", NULL);
  check_simple(&s_ulong, "18446744073709551615", "111111111111111 0 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111 1111", NULL);
  check_simple(&s_svlu5, "16", "0 10 00010000", NULL);
  check_simple(&s_svlu5, "-3", "1 00011", NULL);
  check_simple(&s_ushort, "2", "0000000000000010", NULL);
  check_simple(&s_ubyte, "255", "11111111", NULL);
  check_simple(&s_int, "-2147483648", "00000", NULL);
  check_simple(&s_int, "1", "11111110 1000 0000 0000 0000 0000 0000 0000 0001", NULL);
  check_simple(&s_float, "1.5", "00111111110000000000000000000000", NULL);
  check_simple(&s_double, "-2", "1100000000000000000000000000000000000000000000000000000000000000", NULL);
  check_simple(&r1, "-100", "00000000", NULL);
  check_simple(&r1, "100", "11001000", NULL);
  check_simple(&r2, "15", "1110", NULL);
  check_simple(&r3, "360", "101101000", NULL);
  check_simple(&r4, "-1", "01111111", NULL);
  check_simple(&r5, "70000", "11110 00010001000101110000", NULL);
}
END_TEST

/* ISO 23001-1 7.5.4.1: vluimsbf5 count, bytes or bits */
START_TEST(string_and_binary_codings_follow_iso_bim) {
  static const bim_simple_t hex = {.codec = BIM_HEXBIN};
  static const bim_simple_t b64 = {.codec = BIM_BASE64};
  check_simple(&s_utf8, "Ab\xc3\xa9", "00100 01000001 01100010 11000011 10101001", NULL);
  check_simple(&s_utf8, "", "00000", NULL);
  check_simple(&hex, "0aFF", "10 00010000 00001010 11111111", "0AFF");
  check_simple(&b64, "AQID", "10 00011000 00000001 00000010 00000011", NULL);
  ck_assert_int_eq(encode_simple(&hex, "ABC"), -1);
  ck_assert_int_eq(encode_simple(&hex, "zz"), -1);
  ck_assert_int_eq(encode_simple(&b64, "A*=="), -1);
}
END_TEST

/* ISO 23001-1 7.5.4.2.2 .. 7.5.4.2.4 */
START_TEST(enum_list_union_follow_iso_bim) {
  static const char *const vals[] = {"a", "b", "c"};
  static const bim_simple_t en = {.codec = BIM_ENUM, .enums = vals, .nenums = 3};
  static const bim_simple_t lst = {.codec = BIM_LIST, .range = {1, 2}, .item = &s_ubyte};
  static const bim_simple_t open = {.codec = BIM_LIST, .unbounded = 1, .item = &en};
  static const bim_simple_t *const mem[] = {&en, &s_ubyte, &s_utf8};
  static const bim_simple_t un = {.codec = BIM_UNION, .members = mem, .nmembers = 3};
  check_simple(&en, "c", "10", NULL);
  ck_assert_int_eq(encode_simple(&en, "d"), -1);
  check_simple(&lst, " 1  2 3 ", "10 00000001 00000010 00000011", "1 2 3");
  check_simple(&lst, "7", "00 00000111", NULL);
  ck_assert_int_eq(encode_simple(&lst, ""), -1);
  ck_assert_int_eq(encode_simple(&lst, "1 2 3 4"), -1);
  check_simple(&open, "b a", "00010 01 00", NULL);
  check_simple(&un, "b", "00 01", NULL);
  check_simple(&un, "200", "01 11001000", NULL);
  check_simple(&un, "x", "10 00001 01111000", NULL);
}
END_TEST

static int has_colon(const void *p, const char *v) { return (p != NULL) == (strchr(v, ':') != NULL); }

/* XML Schema Part 2 2.5.1.3: first member whose facets validate, patterns and length */
START_TEST(union_member_by_facets) {
  static const void *const yes[] = {"colon"};
  static const void *const no[] = {NULL};
  static const bim_simple_t v4 = {.codec = BIM_UTF8, .patterns = no, .npatterns = 1, .match = has_colon};
  static const bim_simple_t v6 = {.codec = BIM_UTF8, .patterns = yes, .npatterns = 1, .match = has_colon, .has_length = 1, .maxlen = 4};
  static const bim_simple_t *const mem[] = {&v4, &v6};
  static const bim_simple_t un = {.codec = BIM_UNION, .members = mem, .nmembers = 2};
  check_simple(&un, "1.2", "0 00011 00110001 00101110 00110010", NULL);
  check_simple(&un, "a::b", "1 00100 01100001 00111010 00111010 01100010", NULL);
  ck_assert_int_eq(encode_simple(&un, "a::bc"), -1);
}
END_TEST

static int only_date(const void *p, const char *v) {
  (void)p;
  return strspn(v, "0123456789-") == strlen(v);
}

static int no_frames_or_zone(const void *p, const char *v) {
  (void)p;
  return !strchr(v, 'f') && !strchr(v, '+');
}

/* encode with compat, check bits after DecodingModes, decode back */
static void check_compat(const bim_simple_t *s, unsigned compat, const char *text, const char *bits) {
  const bim_type_t type = {.content = BIM_CONTENT_SIMPLE, .simple = s};
  bim_encode_opts_t eo = {.compat = compat, .lax_schema = -1};
  bim_decode_opts_t dop = {.compat = compat, .lax_schema = -1};
  bim_node_t *n = bim_node_new("X");
  bim_node_t *got = NULL;
  bitwriter_t bw;
  bitwriter_t exp;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *w;
  const unsigned char *str;
  size_t blen;
  size_t wlen;
  size_t slen;
  ck_assert_int_eq(bim_node_set_text(n, text), 0);
  bitwriter_init(&bw);
  bitwriter_init(&exp);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bim_encode_payload_opts(&type, n, &eo, &bw, &sw), 0);
  bitwriter_put(&exp, 0x0F, 8);
  for (const char *p = bits; *p; p++)
    if (*p != ' ') bitwriter_put(&exp, *p == '1', 1);
  b = bitwriter_data(&bw, &blen);
  w = bitwriter_data(&exp, &wlen);
  ck_assert_uint_eq(blen, wlen);
  ck_assert_mem_eq(b, w, wlen);
  str = strrepo_writer_data(&sw, &slen);
  bitreader_init(&br, w, wlen);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, slen), 0);
  ck_assert_int_eq(bim_decode_payload(&type, "X", &dop, &br, &sr, &got), 0);
  ck_assert_str_eq(got->text, text);
  bim_node_free(got);
  bim_node_free(n);
  bitwriter_free(&bw);
  bitwriter_free(&exp);
  strrepo_writer_free(&sw);
}

/* reference software: flags per section the derived type allows, sign flag first, month and day 0-based */
START_TEST(compat_time_flags_follow_the_derived_type) {
  static const void *const pat[] = {"p"};
  static const bim_simple_t date = {.codec = BIM_M7_TIMEPOINT, .patterns = pat, .npatterns = 1, .match = only_date};
  static const bim_simple_t dur = {.codec = BIM_M7_DURATION, .patterns = pat, .npatterns = 1, .match = no_frames_or_zone};
  const bim_type_t type = {.content = BIM_CONTENT_SIMPLE, .simple = &date};
  /* flags neg date month day, year 2001, month 6 - 1, day 16 - 1 */
  check_compat(&date, BIM_COMPAT_M7_FLAGS, "2001-06-16", "0 1 1 1 110 011111010001 0101 01111");
  /* flags neg D T H M S N F, minutes vluimsbf5 1, seconds 6 bits */
  check_compat(&dur, BIM_COMPAT_M7_FLAGS, "PT1M30S", "0 0 1 0 1 1 0 0 00001 011110");
  /* standard: all flags, month and day as written, patterns irrelevant */
  check_compat(&dur, 0, "PT1M30S", "0 0 1 0 1 1 0 0 0 0 00001 011110");
  check_compat(&date, 0, "2001-06-16", "1 0 1 1 0 0 0 110 011111010001 0110 10000");
  ck_assert_int_eq(encode_type(&type, "T20:00"), 0);
  {
    bim_encode_opts_t eo = {.compat = BIM_COMPAT_M7_FLAGS, .lax_schema = -1};
    bim_node_t *n = bim_node_new("X");
    bitwriter_t bw;
    strrepo_writer_t sw;
    ck_assert_int_eq(bim_node_set_text(n, "T20:00"), 0);
    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    /* uncoded section unusable */
    ck_assert_int_eq(bim_encode_payload_opts(&type, n, &eo, &bw, &sw), -1);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    bim_node_free(n);
  }
}
END_TEST

/* ISO 15938-1:2002 8.5.4.2.5, 8.5.4.2.6 */
START_TEST(mpeg7_time_codecs_round_trip) {
  static const bim_simple_t tp = {.codec = BIM_M7_TIMEPOINT};
  static const bim_simple_t du = {.codec = BIM_M7_DURATION};
  /* flags 1 0 1 1, 1 1 0, 0 1. year 2000, month 10, day 9, 18, 00, tz -8+15, 0 */
  check_simple(&tp, "2000-10-09T18:00-08:00", "1 0 1 1 1 1 0 0 1 110 0111 1101 0000 1010 01001 10010 000000 00111 00", NULL);
  check_simple(&tp, "T08:00:01:12.05F25", "0 1 1 1 1 1 1 0 01000 000000 000001 10 00011001 01100 0000101", NULL);
  check_simple(&tp, "-0044", "1 1 0 0 0 0 10 00101100", "-44");
  ck_assert_int_eq(encode_simple(&tp, "2000-1-09"), -1);
  ck_assert_int_eq(encode_simple(&tp, "T25:00"), -1);
  ck_assert_int_eq(encode_simple(&tp, "T08:00:01:12"), -1);
  /* flags 0 0 1, 1 1 0 0, 0, 0 0. hours vluimsbf5 1, minutes 6 bits 30 */
  check_simple(&du, "PT1H30M", "0 0 1 1 1 0 0 0 0 0 00001 011110", NULL);
  check_simple(&du, "-P1DT2H", "1 1 1 1 0 0 0 0 0 0 00001 00010", NULL);
  check_simple(&du, "PT1N25F", "0 0 1 0 0 0 1 0 1 0 10 00011001 00001", NULL);
  check_simple(&du, "P5D+01:00Z", "0 1 0 0 1 00101 100000 00", NULL);
  ck_assert_int_eq(encode_simple(&du, "P"), 0);
  ck_assert_int_eq(encode_simple(&du, "PT"), 0);
  ck_assert_int_eq(encode_simple(&du, "1D"), -1);
  ck_assert_int_eq(encode_simple(&du, "PT1M1H"), -1);
}
END_TEST

static const bim_type_t mode_base = {.content = BIM_CONTENT_SIMPLE, .simple = &s_string};
static const bim_attr_t mode_sub_attrs[] = {{.name = "a", .required = 1, .simple = &s_bool}};
static const bim_type_t mode_sub = {.attrs = mode_sub_attrs, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_string};
static const bim_type_t *const mode_subs_list[] = {NULL, &mode_sub};
static const bim_subtypes_t mode_subs[] = {{&mode_base, mode_subs_list, 2, NULL}};
static const bim_attr_t mode_ext_attrs[] = {{.name = "a", .required = 1, .simple = &s_bool}, {.name = "b", .required = 1, .simple = &s_bool}};
static const bim_type_t mode_ext = {.attrs = mode_ext_attrs, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_string};
static const bim_type_t mode_ext_delta = {.attrs = mode_ext_attrs + 1, .nattrs = 1, .content = BIM_CONTENT_NONE};
static const bim_type_t *const mode_ext_list[] = {&mode_ext};
static const bim_type_t *const mode_ext_deltas[] = {&mode_ext_delta};
static const bim_subtypes_t mode_ext_subs[] = {{&mode_base, mode_ext_list, 1, mode_ext_deltas}};
static const bim_schema_t mode_one[] = {{.subs = mode_subs, .nsubs = 1, .known = 1}};
static const bim_schema_t mode_two[] = {{.subs = mode_subs, .nsubs = 1, .known = 1}, {.subs = mode_ext_subs, .nsubs = 1, .known = 1}};
static const bim_schema_t mode_two_unknown[] = {{.subs = mode_subs, .nsubs = 1, .known = 1}, {.known = 0}};
static const bim_part_t mode_k1[] = {{.kind = BIM_PART_ELEM, .name = "E", .type = &mode_base, .min = 1, .max = 2}};
static const bim_part_t mode_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = mode_k1, .nkids = 1};
static const bim_type_t mode_root = {.content = BIM_CONTENT_SEQ, .top = &mode_top};

/* repository "x" "y" */
static int decode_schemas_compat(const bim_schema_t *schemas, int n, unsigned modes, unsigned compat, const char *bits, bim_node_t **got) {
  bim_decode_opts_t opts = {.lax_schema = -1, .compat = compat};
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *str;
  size_t blen;
  size_t slen;
  int rc;
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "x");
  strrepo_writer_put(&sw, "y");
  bitwriter_put(&bw, modes, 8);
  for (const char *p = bits; *p; p++) bitwriter_put(&bw, *p == '1', 1);
  b = bitwriter_data(&bw, &blen);
  str = strrepo_writer_data(&sw, &slen);
  bitreader_init(&br, b, blen);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, slen), 0);
  opts.schemas = schemas;
  opts.nschemas = n;
  rc = bim_decode_payload(&mode_root, "R", &opts, &br, &sr, got);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

static int decode_schemas(const bim_schema_t *schemas, int n, unsigned modes, const char *bits, bim_node_t **got) { return decode_schemas_compat(schemas, n, modes, 0, bits, got); }

static int decode_modes(unsigned modes, const char *bits, bim_node_t **got) { return decode_schemas(mode_one, 1, modes, bits, got); }

/* conformance streams: no SchemaModeUpdate with two schemas */
START_TEST(compat_drops_schema_mode_bits) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_schemas_compat(mode_two, 2, 0x0F, 0, "0" "0" "0", &got), 0);
  ck_assert_str_eq(got->kids[0]->text, "x");
  bim_node_free(got);
  ck_assert_int_eq(decode_schemas_compat(mode_two, 2, 0x0F, BIM_COMPAT_NO_SCHEMA_MODE, "0", &got), 0);
  ck_assert_uint_eq(got->nkids, 1);
  ck_assert_str_eq(got->kids[0]->text, "x");
  bim_node_free(got);
}
END_TEST

/* ISO 15938-1 8.4.5: code over {abstract, mode_sub} */
START_TEST(decoder_honors_type_cast) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_modes(0x1F, "11110", &got), 0);
  ck_assert_uint_eq(got->nkids, 2);
  ck_assert_str_eq(bim_node_attr(got->kids[0], "a"), "true");
  ck_assert_str_eq(got->kids[0]->text, "x");
  ck_assert_ptr_null(bim_node_attr(got->kids[1], "a"));
  ck_assert_str_eq(got->kids[1]->text, "y");
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0x1F, "010", &got), -1);
  ck_assert_int_eq(decode_modes(0x0F, "1", &got), 0);
  ck_assert_uint_eq(got->nkids, 2);
  bim_node_free(got);
}
END_TEST

/* ISO 15938-1 6.7.2, 8.4.5, ISO 23001-1 6.4.1: deferred nodes are instantiated */
START_TEST(decoder_keeps_deferred_nodes) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_modes(0x2F, "110", &got), 0);
  ck_assert_uint_eq(got->nkids, 2);
  ck_assert_int_eq(got->kids[0]->deferred, 1);
  ck_assert_ptr_null(got->kids[0]->text);
  ck_assert_str_eq(got->kids[1]->text, "x");
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0x3F, "01100", &got), 0);
  ck_assert_uint_eq(got->nkids, 1);
  ck_assert_int_eq(got->kids[0]->deferred, 0);
  ck_assert_str_eq(bim_node_attr(got->kids[0], "a"), "false");
  bim_node_free(got);
}
END_TEST

/* ISO 15938-1 8.4.2 */
START_TEST(decoder_checks_element_length) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_modes(0x8F, "00110000000", &got), 0);
  ck_assert_uint_eq(got->nkids, 1);
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0x8F, "00101000000", &got), -1);
  ck_assert_int_eq(decode_modes(0x4F, "00100000", &got), 0);
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0xCF, "0", &got), -1);
}
END_TEST

/* ISO 15938-1 8.4.1, 8.4.3, 8.4.4: chunk 0 base, chunk 1 delta @b */
START_TEST(decoder_multi_version_chunks) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_schemas(mode_two, 2, 0x0F, "00100010100011", &got), 0);
  ck_assert_uint_eq(got->nkids, 1);
  ck_assert_str_eq(got->kids[0]->text, "x");
  ck_assert_str_eq(bim_node_attr(got->kids[0], "b"), "true");
  bim_node_free(got);
  ck_assert_int_eq(decode_schemas(mode_two, 2, 0x0F, "00100010000011", &got), -1);
  ck_assert_int_eq(decode_schemas(mode_two, 2, 0x0F, "000", &got), 0);
  ck_assert_ptr_null(bim_node_attr(got->kids[0], "b"));
  bim_node_free(got);
}
END_TEST

/* TS 102 323 9.4.4, table 58 */
START_TEST(decoder_skips_unknown_schema_chunk) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_schemas(mode_two_unknown, 2, 0x0F, "0110001010001100000000000000010", &got), 0);
  ck_assert_uint_eq(got->nkids, 2);
  ck_assert_str_eq(got->kids[0]->text, "x");
  ck_assert_ptr_null(bim_node_attr(got->kids[0], "b"));
  ck_assert_str_eq(got->kids[1]->text, "y");
  bim_node_free(got);
}
END_TEST

/* TS 102 323 table 58 */
START_TEST(decoder_resyncs_strings_after_skippable_element) {
  bim_node_t *got = NULL;
  ck_assert_int_eq(decode_modes(0x4F, "0110000000000000000000010", &got), 0);
  ck_assert_uint_eq(got->nkids, 2);
  ck_assert_str_eq(got->kids[0]->text, "x");
  ck_assert_str_eq(got->kids[1]->text, "y");
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0x4F, "0110000000000000000000000", &got), 0);
  ck_assert_str_eq(got->kids[1]->text, "x");
  bim_node_free(got);
  ck_assert_int_eq(decode_modes(0x4F, "0110000000000000000000101", &got), -1);
}
END_TEST

/* ISO 15938-1 8.5.2 */
START_TEST(choice_branch_occurrences_round_trip) {
  static const bim_part_t kids[] = {{.kind = BIM_PART_ELEM, .name = "A", .type = &mode_base, .min = 1, .max = BIM_UNBOUNDED},
                                    {.kind = BIM_PART_ELEM, .name = "B", .type = &mode_base, .min = 1, .max = 1}};
  static const bim_part_t top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = kids, .nkids = 2};
  static const bim_type_t root = {.content = BIM_CONTENT_SEQ, .top = &top};
  bim_node_t *n = bim_node_new("R");
  bim_node_t *got = NULL;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *str;
  size_t blen;
  size_t slen;
  ck_assert_ptr_nonnull(n);
  for (int i = 0; i < 3; i++) ck_assert_ptr_nonnull(bim_node_add_new(n, "A", "x"));
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bim_encode_payload(&root, n, 0, &bw, &sw), 0);
  b = bitwriter_data(&bw, &blen);
  ck_assert_uint_eq(blen, 2);
  ck_assert_uint_eq(b[1] >> 2, 0x02); /* code 0, count-1 = 2 as vluimsbf5 */
  str = strrepo_writer_data(&sw, &slen);
  bitreader_init(&br, b, blen);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, slen), 0);
  ck_assert_int_eq(bim_decode_payload(&root, "R", NULL, &br, &sr, &got), 0);
  ck_assert_uint_eq(got->nkids, 3);
  bim_node_free(got);
  bim_node_free(n);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

/* minOccurs 0, maxOccurs > 1: shunt bit in both editions (ISO 15938-1:2002 8.5.2.2.5.2 case c-2) */
START_TEST(occurrence_shunt_in_both_editions) {
  static const bim_part_t kids[] = {{.kind = BIM_PART_ELEM, .name = "A", .type = &mode_base, .min = 0, .max = BIM_UNBOUNDED}};
  static const bim_part_t top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = kids, .nkids = 1};
  static const bim_type_t root = {.content = BIM_CONTENT_SEQ, .top = &top};
  bim_node_t *n = bim_node_new("R");
  for (int mpegb = 0; mpegb < 2; mpegb++) {
    bim_decode_opts_t opts = {.mpegb = mpegb, .lax_schema = -1};
    bim_node_t *got = NULL;
    bitwriter_t bw;
    bitreader_t br;
    strrepo_writer_t sw;
    const unsigned char *b;
    size_t blen;
    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    ck_assert_int_eq(bim_encode_payload(&root, n, mpegb, &bw, &sw), 0);
    b = bitwriter_data(&bw, &blen);
    ck_assert_uint_eq(blen, 2);
    ck_assert_uint_eq(b[1], 0x00);
    bitreader_init(&br, b, blen);
    ck_assert_int_eq(bim_decode_payload(&root, "R", &opts, &br, NULL, &got), 0);
    ck_assert_uint_eq(got->nkids, 0);
    ck_assert_uint_eq(bitreader_bits_left(&br), 7);
    bim_node_free(got);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
  bim_node_free(n);
}
END_TEST

/* attributes matched by expanded name */
START_TEST(attributes_match_namespace) {
  static const bim_attr_t attrs[] = {{.name = "a", .simple = &s_bool}, {.name = "a", .ns = "urn:x", .simple = &s_bool}};
  static const bim_type_t t = {.attrs = attrs, .nattrs = 2, .content = BIM_CONTENT_NONE};
  bim_node_t *n = bim_node_new("R");
  bim_node_t *got = NULL;
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  const unsigned char *b;
  size_t blen;
  ck_assert_int_eq(bim_node_add_attr_ns(n, "urn:x", "a", strdup("true")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(n, "urn:x", "a", strdup("false")), -2);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bim_encode_payload(&t, n, 0, &bw, &sw), 0);
  b = bitwriter_data(&bw, &blen);
  ck_assert_uint_eq(b[1] >> 5, 0x03); /* absent, present, true */
  bitreader_init(&br, b, blen);
  ck_assert_int_eq(bim_decode_payload(&t, "R", NULL, &br, NULL, &got), 0);
  ck_assert_ptr_null(bim_node_attr_ns(got, NULL, "a"));
  ck_assert_str_eq(bim_node_attr_ns(got, "urn:x", "a"), "true");
  bim_node_free(got);
  bim_node_free(n);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(boolean_encode_accepts_xml_lexical_forms) {
  const char *good[] = {"true", "false", "1", "0", " true\n"};
  for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) ck_assert_int_eq(encode_simple(&s_bool, good[i]), 0);
}
END_TEST

START_TEST(boolean_encode_rejects_garbage) {
  const char *bad[] = {"", "yes", "TRUE", "2", "-1", "truee", "10", "tru"};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_int_eq(encode_simple(&s_bool, bad[i]), -1);
}
END_TEST

static Suite *codec_suite(void) {
  Suite *s = suite_create("bim_codec");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, dvb_string_round_trips);
  tcase_add_test(tc, dvb_locator_round_trips_string_fallback);
  tcase_add_test(tc, dvb_locator_decode_rejects_value_longer_than_buffer);
  tcase_add_test(tc, dvb_datetime_round_trips_utc);
  tcase_add_test(tc, dvb_datetime_normalizes_offset_to_utc_across_midnight);
  tcase_add_test(tc, dvb_datetime_no_offset_is_utc);
  tcase_add_test(tc, dvb_datetime_published_reuses_date_and_keeps_seconds);
  tcase_add_test(tc, dvb_datetime_encode_rejects_malformed_input);
  tcase_add_test(tc, dvb_datetime_decode_rejects_ms_out_of_day);
  tcase_add_test(tc, dvb_datetime_decode_rejects_mjd_out_of_range);
  tcase_add_test(tc, dvb_datetime_encode_rejects_invalid_fields);
  tcase_add_test(tc, dvb_datetime_encode_rejects_date_out_of_range);
  tcase_add_test(tc, dvb_controlledterm_round_trips_dotted_term);
  tcase_add_test(tc, dvb_controlledterm_round_trips_unknown_scheme_as_string);
  tcase_add_test(tc, dvb_controlledterm_decode_rejects_compact_form);
  tcase_add_test(tc, vluimsbf5_range_accepts_plain_and_padded_numbers);
  tcase_add_test(tc, vluimsbf5_range_rejects_garbage);
  tcase_add_test(tc, svlu5_and_fixed_ranges_encode_bounds);
  tcase_add_test(tc, range_encode_rejects_out_of_bounds);
  tcase_add_test(tc, integer_codings_follow_iso_bim);
  tcase_add_test(tc, string_and_binary_codings_follow_iso_bim);
  tcase_add_test(tc, enum_list_union_follow_iso_bim);
  tcase_add_test(tc, union_member_by_facets);
  tcase_add_test(tc, mpeg7_time_codecs_round_trip);
  tcase_add_test(tc, compat_time_flags_follow_the_derived_type);
  tcase_add_test(tc, compat_drops_schema_mode_bits);
  tcase_add_test(tc, decoder_honors_type_cast);
  tcase_add_test(tc, decoder_keeps_deferred_nodes);
  tcase_add_test(tc, decoder_checks_element_length);
  tcase_add_test(tc, decoder_resyncs_strings_after_skippable_element);
  tcase_add_test(tc, decoder_multi_version_chunks);
  tcase_add_test(tc, decoder_skips_unknown_schema_chunk);
  tcase_add_test(tc, choice_branch_occurrences_round_trip);
  tcase_add_test(tc, occurrence_shunt_in_both_editions);
  tcase_add_test(tc, attributes_match_namespace);
  tcase_add_test(tc, boolean_encode_accepts_xml_lexical_forms);
  tcase_add_test(tc, boolean_encode_rejects_garbage);
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
