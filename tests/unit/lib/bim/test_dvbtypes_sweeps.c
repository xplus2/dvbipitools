/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec.h"

static const char *const locators[] = {
  "dvb://233a..1", "dvb://233a.1.2", "dvb://233a.1.2.3", "dvb://233a.1.2.3&4&5", "dvb://233a.1.2.3&4$ff", "dvb://233a.1.2;6", "dvb://233a.1.2;;7", "dvb://233a.1.2.3;4~20300101T120000Z--PT01H00M00S",
  "dvb://233a.1.2.3&4$5;;9~20300102T000001Z--PT00H30M10S/path/x", "dvb://233a.1.2/p", "dvb://1.2.3/p/q", "dvb://1.2.3", "dvb://1.2.3~20300102T100000Z--PT00H00M01S", "rtp://239.1.1.1:5000", "dvb://", "dvb://zz.1.2",
  "dvb://1.2", "dvb://1.2.3.", "dvb://1.2.3~bogus", "dvb://1.2.3~19000101T000000Z--PT00H00M00S", "dvb://1.2.3~20300101T250000Z--PT00H00M00S", "dvb://1.2.3.1$", "dvb://1.2.3;", "dvb://1.2.3x",
  "dvb://ffffffffff.1.2", "dvb://1.2.3.1&2&3&4&5&6&7&8&9&a&b&c&d&e&f&10&11&12&13&14&15&16&17&18&19&1a&1b&1c&1d&1e&1f&20&21",
};

static const char *const datetimes[] = {
  "2030-01-01T12:00:00Z", "2030-01-01T12:00:30Z", "2030-01-01T12:01:00Z", "2030-01-02T00:00:00Z", "2030-01-01T14:00:00+02:00", "2030-01-01T10:00:00-02:00", "2030-01-01T12:00:00", "1900-03-01T00:00:00Z",
  "1899-01-01T00:00:00Z", "2100-03-01T00:00:00Z", "2100-02-28T23:59:59Z", "1969-12-31T23:59:00Z", "2040-06-15T08:30:00Z", "bogus", "2030-13-01T00:00:00Z", "",
};

static const char *const durations[] = {"PT1H30M", "PT0H0M", "PT34H59M", "PT35H0M", "PT1H60M", "PT1H5M7S", "P1D", "PT01H30M", "PT1H", "", "PT2047M", "PT1H30MX"};

static const char *const terms[] = {
  "urn:tva:metadata:cs:ContentCS:2004:3.1.4", "urn:tva:metadata:cs:ActionTypeCS:2004:1", "urn:tva:metadata:cs:Unknown:2004:1", "urn:other:cs:X:5", "plain", "urn:tva:metadata:cs:ContentCS:2004:", "urn:mpeg:mpeg7:cs:RoleCS:2001:2",
  "urn:tva:metadata:cs:UnitTypeCS:2004:9",
};

static int rank_of(void *ctx, const char *uri, const char *term, uint64_t *rank) {
  (void)ctx;
  (void)uri;
  if (!term[0]) return -1;
  *rank = (uint64_t)strlen(term);
  return 0;
}

static int name_of(void *ctx, const char *uri, uint64_t rank, char *name, size_t cap) {
  (void)ctx;
  (void)uri;
  if (rank == 0 || rank > 200) return -1;
  snprintf(name, cap, "t%llu", (unsigned long long)rank);
  return 0;
}

static void decode_locators(const unsigned char *b, size_t n, const unsigned char *s, size_t sl, int prime) {
  bitreader_t br;
  strrepo_reader_t sr;
  dvb_codec_state_t st;
  char out[512];
  memset(&st, 0, sizeof st);
  if (prime) {
    st.have_prefix = 1;
    st.onid = 1;
    st.tsid = 2;
    st.sid = 3;
    st.have_day = 1;
    st.day = 1000;
  }
  bitreader_init(&br, b, n);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  for (int i = 0; i < 40; i++)
    if (dvb_locator_decode_st(&br, &sr, &st, out, sizeof out)) break;
  bitreader_init(&br, b, n);
  (void)dvb_locator_decode_st(&br, NULL, &st, out, 8);
  bitreader_init(&br, b, n);
  (void)dvb_locator_decode(&br, &sr, out, sizeof out);
}

START_TEST(dvbtypes_locators_round_trip_and_survive_damage) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  dvb_codec_state_t st;
  const unsigned char *p;
  const unsigned char *q;
  unsigned char *copy;
  size_t n;
  size_t qn;
  memset(&st, 0, sizeof st);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  for (size_t i = 0; i < sizeof locators / sizeof *locators; i++) ck_assert_int_eq(dvb_locator_encode_st(&bw, &sw, &st, locators[i], 1), 0);
  for (size_t i = 0; i < sizeof locators / sizeof *locators; i++) ck_assert_int_eq(dvb_locator_encode_st(&bw, &sw, &st, locators[i], 0), 0);
  p = bitwriter_data(&bw, &n);
  q = strrepo_writer_data(&sw, &qn);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  decode_locators(copy, n, q, qn, 0);
  decode_locators(copy, n, q, qn, 1);
  for (size_t cut = 0; cut < n; cut += 3) decode_locators(copy, cut, q, qn, 0);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    decode_locators(copy, n, q, qn, bit % 2);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  free(copy);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(dvbtypes_datetimes_round_trip_and_survive_damage) {
  bitwriter_t bw;
  dvb_codec_state_t st;
  const unsigned char *p;
  unsigned char *copy;
  size_t n;
  for (int published = 0; published < 2; published++) {
    memset(&st, 0, sizeof st);
    bitwriter_init(&bw);
    for (size_t i = 0; i < sizeof datetimes / sizeof datetimes[0]; i++) (void)dvb_datetime_encode_st(&bw, &st, datetimes[i], published);
    (void)dvb_datetime_encode(&bw, datetimes[0]);
    p = bitwriter_data(&bw, &n);
    copy = malloc(n);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, p, n);
    for (size_t bit = 0; bit <= n * 8; bit++) {
      bitreader_t br;
      dvb_codec_state_t d;
      char out[64];
      if (bit < n * 8) copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      for (int prime = 0; prime < 2; prime++) {
        memset(&d, 0, sizeof d);
        d.have_date = prime;
        d.mjd = 60000;
        bitreader_init(&br, copy, n);
        for (int k = 0; k < 20; k++)
          if (dvb_datetime_decode_st(&br, &d, out, prime ? sizeof out : 12)) break;
      }
      if (bit < n * 8) copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    for (size_t cut = 0; cut < n; cut++) {
      bitreader_t br;
      dvb_codec_state_t d;
      char out[64];
      memset(&d, 0, sizeof d);
      bitreader_init(&br, copy, cut);
      for (int k = 0; k < 20; k++)
        if (dvb_datetime_decode_st(&br, &d, out, sizeof out)) break;
    }
    free(copy);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(dvbtypes_durations_and_terms_round_trip_and_survive_damage) {
  dvb_cs_t cs;
  const char *list[2] = {"urn:tva:metadata:cs:ContentCS:2004", "urn:other:cs:X"};
  memset(&cs, 0, sizeof cs);
  cs.rank = rank_of;
  cs.term = name_of;
  cs.list = list;
  cs.nlist = 2;
  cs.compact = 1;
  for (int variant = 0; variant < 3; variant++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    const unsigned char *p;
    const unsigned char *q;
    unsigned char *copy;
    size_t n;
    size_t qn;
    const dvb_cs_t *use = variant == 0 ? NULL : &cs;
    if (variant == 2) cs.compact = 0;
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    for (size_t i = 0; i < sizeof terms / sizeof terms[0]; i++) ck_assert_int_eq(dvb_controlledterm_encode_cs(&bw, &sw, use, terms[i]), 0);
    for (size_t i = 0; i < sizeof durations / sizeof durations[0]; i++) ck_assert_int_eq(dvb_duration_encode(&bw, &sw, durations[i], variant & 1), 0);
    p = bitwriter_data(&bw, &n);
    q = strrepo_writer_data(&sw, &qn);
    copy = malloc(n);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, p, n);
    for (size_t bit = 0; bit <= n * 8; bit++) {
      bitreader_t br;
      strrepo_reader_t sr;
      char out[300];
      if (bit < n * 8) copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      bitreader_init(&br, copy, n);
      ck_assert_int_eq(strrepo_reader_init(&sr, q, qn), 0);
      for (size_t i = 0; i < sizeof terms / sizeof terms[0]; i++)
        if (dvb_controlledterm_decode_cs(&br, &sr, use, out, bit % 2 ? sizeof out : 10)) break;
      bitreader_init(&br, copy, n);
      (void)dvb_controlledterm_decode(&br, &sr, out, sizeof out);
      if (bit < n * 8) copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    for (size_t cut = 0; cut < n; cut++) {
      bitreader_t br;
      strrepo_reader_t sr;
      char out[300];
      bitreader_init(&br, copy, cut);
      ck_assert_int_eq(strrepo_reader_init(&sr, q, qn), 0);
      for (size_t i = 0; i < sizeof terms / sizeof terms[0]; i++)
        if (dvb_controlledterm_decode_cs(&br, &sr, use, out, sizeof out)) break;
    }
    free(copy);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
}
END_TEST

START_TEST(dvbtypes_encoders_survive_allocation_failures) {
  for (long k = 0;; k++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    dvb_codec_state_t st;
    int hit;
    memset(&st, 0, sizeof st);
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    alloc_fail_at(k);
    for (size_t i = 0; i < sizeof locators / sizeof locators[0]; i++) (void)dvb_locator_encode_st(&bw, &sw, &st, locators[i], 1);
    for (size_t i = 0; i < sizeof datetimes / sizeof datetimes[0]; i++) (void)dvb_datetime_encode_st(&bw, &st, datetimes[i], 1);
    for (size_t i = 0; i < sizeof durations / sizeof durations[0]; i++) (void)dvb_duration_encode(&bw, &sw, durations[i], 1);
    hit = alloc_fail_end();
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    if (!hit) break;
  }
}
END_TEST

static Suite *dvbtypes_suite(void) {
  Suite *s = suite_create("bim_dvbtypes_sweeps");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, dvbtypes_locators_round_trip_and_survive_damage);
  tcase_add_test(tc, dvbtypes_datetimes_round_trip_and_survive_damage);
  tcase_add_test(tc, dvbtypes_durations_and_terms_round_trip_and_survive_damage);
  tcase_add_test(tc, dvbtypes_encoders_survive_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dvbtypes_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
