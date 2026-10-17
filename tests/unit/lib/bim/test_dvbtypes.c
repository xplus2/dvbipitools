/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define MAXF 160

typedef struct {
  uint64_t v;
  int n;
} fld_t;

typedef struct {
  int prefix;
  unsigned ts, on, sid;
  int nctag;
  int cid_present;
  unsigned cid;
  int evt;
  unsigned evt_id;
  int time;
  int day_present;
  unsigned day, tm, dur;
  int path;
} locspec_t;

static size_t loc_fields(const locspec_t *s, fld_t *f) {
  size_t n = 0;
  f[n++] = (fld_t){1, 1};
  f[n++] = (fld_t){(uint64_t)s->prefix, 1};
  if (s->prefix) {
    f[n++] = (fld_t){s->ts, 16};
    f[n++] = (fld_t){s->on, 16};
    f[n++] = (fld_t){s->sid, 16};
  }
  f[n++] = (fld_t){s->nctag ? 1 : 0, 1};
  for (int i = 0; i < s->nctag; i++) {
    f[n++] = (fld_t){(uint64_t)(i + 1), 8};
    f[n++] = (fld_t){i + 1 < s->nctag ? 1 : 0, 1};
  }
  f[n++] = (fld_t){(uint64_t)s->cid_present, 1};
  if (s->cid_present) f[n++] = (fld_t){s->cid, 32};
  f[n++] = (fld_t){(uint64_t)s->evt, 2};
  if (s->evt == 1 || s->evt == 2) f[n++] = (fld_t){s->evt_id, 16};
  f[n++] = (fld_t){(uint64_t)s->time, 1};
  if (s->time) {
    f[n++] = (fld_t){(uint64_t)s->day_present, 1};
    if (s->day_present) f[n++] = (fld_t){s->day, 16};
    f[n++] = (fld_t){s->tm, 17};
    f[n++] = (fld_t){s->dur, 17};
  }
  f[n++] = (fld_t){(uint64_t)s->path, 1};
  return n;
}

static void put_fields(bitwriter_t *bw, const fld_t *f, size_t nf) {
  for (size_t i = 0; i < nf; i++) {
    if (f[i].n == -4) ck_assert_int_eq(bitwriter_put_vluimsbf4(bw, f[i].v), 0);
    else if (f[i].n == -8) ck_assert_int_eq(bitwriter_put_vluimsbf8(bw, f[i].v), 0);
    else ck_assert_int_eq(bitwriter_put(bw, f[i].v, f[i].n), 0);
  }
}

static int run_loc(const fld_t *f, size_t nf, const char *path, size_t drop, dvb_codec_state_t *st, char *out, size_t cap) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bd, *sd;
  size_t bl, sl;
  int rc;
  dvb_codec_state_t local;

  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  put_fields(&bw, f, nf);
  ck_assert_int_eq(strrepo_writer_put(&sw, path ? path : ""), 0);
  bd = bitwriter_data(&bw, &bl);
  sd = strrepo_writer_data(&sw, &sl);
  ck_assert_uint_ge(bl, drop);
  bitreader_init(&br, bd, bl - drop);
  ck_assert_int_eq(strrepo_reader_init(&sr, sd, sl), 0);
  if (!st) {
    memset(&local, 0, sizeof local);
    st = &local;
  }
  rc = dvb_locator_decode_st(&br, &sr, st, out, cap);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

static const locspec_t FULL = {1, 0x1234, 0x233a, 0x5678, 2, 1, 0xdeadbeef, 1, 0xabcd, 1, 1, 0x4000, 0x10, 0x20, 1};

static int loc_spec(const locspec_t *s, const char *path, size_t drop, dvb_codec_state_t *st, char *out, size_t cap) {
  fld_t f[MAXF];
  size_t n = loc_fields(s, f);
  return run_loc(f, n, path, drop, st, out, cap);
}

START_TEST(locator_optimized_full_form) {
  char out[256];
  ck_assert_int_eq(loc_spec(&FULL, "a/b", 0, NULL, out, sizeof out), 0);
  ck_assert_str_eq(out, "dvb://233a.1234.5678.1.2;cid=deadbeef;abcd@4000:10--20/a/b");
}
END_TEST

START_TEST(locator_optimized_variants) {
  static const struct {
    locspec_t s;
    const char *path;
    const char *want;
  } t[] = {
      {{1, 0x1234, 0x233a, 0x5678, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, NULL, "dvb://233a.1234.5678"},
      {{1, 0, 0x233a, 0x5678, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, NULL, "dvb://233a..5678"},
      {{1, 1, 2, 3, 0, 0, 0, 2, 0xff, 0, 0, 0, 0, 0, 0}, NULL, "dvb://2.1.3;tva=ff"},
      {{1, 1, 2, 3, 0, 0, 0, 1, 0xff, 0, 0, 0, 0, 0, 0}, NULL, "dvb://2.1.3;ff"},
      {{1, 1, 2, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, NULL, "dvb://2.1.3.1"},
      {{1, 1, 2, 3, 0, 1, 0x10, 0, 0, 0, 0, 0, 0, 0, 0}, NULL, "dvb://2.1.3;cid=10"},
      {{1, 1, 2, 3, 0, 0, 0, 0, 0, 1, 1, 7, 8, 9, 0}, NULL, "dvb://2.1.3@7:8--9"},
      {{1, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, "x", "dvb://2.1.3/x"},
  };
  for (size_t i = 0; i < NELEM(t); i++) {
    char out[128];
    ck_assert_int_eq(loc_spec(&t[i].s, t[i].path, 0, NULL, out, sizeof out), 0);
    ck_assert_str_eq(out, t[i].want);
  }
}
END_TEST

START_TEST(locator_optimized_caps_content_tags_at_32) {
  locspec_t s = FULL;
  char out[512];
  s.nctag = 40;
  s.cid_present = 0;
  s.evt = 0;
  s.time = 0;
  s.path = 0;
  ck_assert_int_eq(loc_spec(&s, NULL, 0, NULL, out, sizeof out), 0);
  ck_assert_ptr_nonnull(strstr(out, ".20"));
  ck_assert_ptr_null(strstr(out, ".21"));
  ck_assert_ptr_null(strstr(out, ".28"));
}
END_TEST

START_TEST(locator_optimized_reuses_prefix_and_day) {
  dvb_codec_state_t st;
  locspec_t a = FULL;
  locspec_t b = FULL;
  char out[256];
  memset(&st, 0, sizeof st);
  ck_assert_int_eq(loc_spec(&a, NULL, 0, &st, out, sizeof out), 0);
  b.prefix = 0;
  b.day_present = 0;
  b.nctag = 0;
  b.cid_present = 0;
  b.evt = 0;
  b.path = 0;
  ck_assert_int_eq(loc_spec(&b, NULL, 0, &st, out, sizeof out), 0);
  ck_assert_str_eq(out, "dvb://233a.1234.5678@4000:10--20");
}
END_TEST

START_TEST(locator_optimized_needs_prefix_and_day_state) {
  locspec_t s = FULL;
  char out[256];
  s.prefix = 0;
  ck_assert_int_eq(loc_spec(&s, NULL, 0, NULL, out, sizeof out), -1);
  s = FULL;
  s.day_present = 0;
  ck_assert_int_eq(loc_spec(&s, NULL, 0, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(locator_optimized_rejects_reserved_event_kind) {
  locspec_t s = FULL;
  fld_t f[MAXF];
  size_t n;
  char out[256];
  s.evt = 3;
  s.time = 0;
  s.path = 0;
  n = loc_fields(&s, f);
  ck_assert_int_eq(run_loc(f, n, NULL, 0, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(locator_optimized_rejects_every_short_output_buffer) {
  char want[256];
  char out[256];
  size_t len;
  ck_assert_int_eq(loc_spec(&FULL, "a/b", 0, NULL, want, sizeof want), 0);
  len = strlen(want);
  for (size_t cap = 0; cap <= len; cap++) ck_assert_int_eq(loc_spec(&FULL, "a/b", 0, NULL, out, cap), -1);
  ck_assert_int_eq(loc_spec(&FULL, "a/b", 0, NULL, out, len + 1), 0);
  ck_assert_str_eq(out, want);
}
END_TEST

START_TEST(locator_optimized_rejects_every_truncation) {
  fld_t f[MAXF];
  size_t n = loc_fields(&FULL, f);
  bitwriter_t bw;
  size_t total;
  char out[256];
  bitwriter_init(&bw);
  put_fields(&bw, f, n);
  (void)bitwriter_data(&bw, &total);
  bitwriter_free(&bw);
  for (size_t drop = 1; drop <= total; drop++) ck_assert_int_eq(run_loc(f, n, "a/b", drop, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(locator_optimized_rejects_oversized_path) {
  char big[400];
  char out[512];
  memset(big, 'p', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(loc_spec(&FULL, big, 0, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(locator_decode_without_state_and_string_branch) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bd, *sd;
  size_t bl, sl;
  char out[64];
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 1, 1), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 1), 0);
  ck_assert_int_eq(strrepo_writer_put(&sw, "s"), 0);
  bd = bitwriter_data(&bw, &bl);
  sd = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, bd, bl);
  ck_assert_int_eq(strrepo_reader_init(&sr, sd, sl), 0);
  ck_assert_int_eq(dvb_locator_decode(&br, &sr, out, sizeof out), -1);
  bitreader_init(&br, bd, 0);
  ck_assert_int_eq(dvb_locator_decode(&br, &sr, out, sizeof out), -1);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(string_decode_dup_handles_empty_and_failure) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *sd;
  size_t sl;
  char *s = malloc(4);
  ck_assert_ptr_nonnull(s);
  memcpy(s, "old", 4);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(dvb_string_encode(&sw, ""), 0);
  ck_assert_int_eq(dvb_string_encode(&sw, "text"), 0);
  sd = strrepo_writer_data(&sw, &sl);
  ck_assert_int_eq(strrepo_reader_init(&sr, sd, sl), 0);
  ck_assert_int_eq(dvb_string_decode_dup(&sr, &s), 0);
  ck_assert_ptr_null(s);
  ck_assert_int_eq(dvb_string_decode_dup(&sr, &s), 0);
  ck_assert_str_eq(s, "text");
  ck_assert_int_eq(dvb_string_decode_dup(&sr, &s), -1);
  ck_assert_str_eq(s, "text");
  free(s);
  strrepo_writer_free(&sw);
}
END_TEST

static int run_dt(const fld_t *f, size_t nf, size_t drop, dvb_codec_state_t *st, char *out, size_t cap) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *bd;
  size_t bl;
  int rc;
  dvb_codec_state_t local;
  bitwriter_init(&bw);
  put_fields(&bw, f, nf);
  bd = bitwriter_data(&bw, &bl);
  ck_assert_uint_ge(bl, drop);
  bitreader_init(&br, bd, bl - drop);
  if (!st) {
    memset(&local, 0, sizeof local);
    st = &local;
  }
  rc = dvb_datetime_decode_st(&br, st, out, cap);
  bitwriter_free(&bw);
  return rc;
}

START_TEST(datetime_published_form) {
  static const struct {
    uint64_t date;
    uint64_t minutes;
    const char *want;
  } t[] = {
      {59198, 765, "2020-12-15T12:45:00Z"},
      {59198, 0, "2020-12-15T00:00:00Z"},
      {59198, 1439, "2020-12-15T23:59:00Z"},
      {1000, 60, "2041-01-17T01:00:00Z"},
  };
  for (size_t i = 0; i < NELEM(t); i++) {
    fld_t f[] = {{1, 2}, {1, 1}, {t[i].date, 16}, {t[i].minutes, 11}};
    char out[32];
    ck_assert_int_eq(run_dt(f, NELEM(f), 0, NULL, out, sizeof out), 0);
    ck_assert_str_eq(out, t[i].want);
  }
}
END_TEST

START_TEST(datetime_published_reuses_date) {
  dvb_codec_state_t st;
  fld_t a[] = {{1, 2}, {1, 1}, {59198, 16}, {60, 11}};
  fld_t b[] = {{1, 2}, {0, 1}, {120, 11}};
  char out[32];
  memset(&st, 0, sizeof st);
  ck_assert_int_eq(run_dt(b, NELEM(b), 0, &st, out, sizeof out), -1);
  ck_assert_int_eq(run_dt(a, NELEM(a), 0, &st, out, sizeof out), 0);
  ck_assert_int_eq(run_dt(b, NELEM(b), 0, &st, out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T02:00:00Z");
}
END_TEST

START_TEST(datetime_rejects_bad_streams) {
  static const struct {
    fld_t f[5];
    size_t n;
  } t[] = {
      {{{1, 2}, {1, 1}, {59198, 16}, {1440, 11}}, 4},
      {{{1, 2}, {1, 1}, {59198, 16}, {2047, 11}}, 4},
      {{{2, 2}}, 1},
      {{{3, 2}}, 1},
      {{{0, 2}, {15078, 32}, {0, 32}}, 3},
      {{{0, 2}, {88128, 32}, {0, 32}}, 3},
      {{{0, 2}, {59198, 32}, {86400000, 32}}, 3},
  };
  char out[32];
  for (size_t i = 0; i < NELEM(t); i++) ck_assert_int_eq(run_dt(t[i].f, t[i].n, 0, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(datetime_accepts_range_boundaries) {
  fld_t lo[] = {{0, 2}, {15079, 32}, {0, 32}};
  fld_t hi[] = {{0, 2}, {88127, 32}, {86399999, 32}};
  char out[32];
  ck_assert_int_eq(run_dt(lo, NELEM(lo), 0, NULL, out, sizeof out), 0);
  ck_assert_str_eq(out, "1900-03-01T00:00:00Z");
  ck_assert_int_eq(run_dt(hi, NELEM(hi), 0, NULL, out, sizeof out), 0);
  ck_assert_str_eq(out, "2100-02-28T23:59:59Z");
}
END_TEST

START_TEST(datetime_rejects_every_truncation) {
  fld_t full[] = {{0, 2}, {59198, 32}, {1000, 32}};
  fld_t pub[] = {{1, 2}, {1, 1}, {59198, 16}, {765, 11}};
  char out[32];
  for (size_t drop = 1; drop <= 8; drop++) ck_assert_int_eq(run_dt(full, NELEM(full), drop, NULL, out, sizeof out), -1);
  for (size_t drop = 1; drop <= 3; drop++) ck_assert_int_eq(run_dt(pub, NELEM(pub), drop, NULL, out, sizeof out), -1);
  ck_assert_int_eq(run_dt(full, 0, 0, NULL, out, sizeof out), -1);
}
END_TEST

START_TEST(datetime_rejects_short_output_buffer) {
  fld_t full[] = {{0, 2}, {59198, 32}, {1000, 32}};
  fld_t pub[] = {{1, 2}, {1, 1}, {59198, 16}, {765, 11}};
  char out[32];
  ck_assert_int_eq(run_dt(full, NELEM(full), 0, NULL, out, 20), -1);
  ck_assert_int_eq(run_dt(full, NELEM(full), 0, NULL, out, 21), 0);
  ck_assert_int_eq(run_dt(pub, NELEM(pub), 0, NULL, out, 20), -1);
  ck_assert_int_eq(run_dt(pub, NELEM(pub), 0, NULL, out, 21), 0);
}
END_TEST

START_TEST(datetime_round_trips_every_month) {
  for (int mo = 1; mo <= 12; mo++) {
    bitwriter_t bw;
    bitreader_t br;
    const unsigned char *bd;
    size_t bl;
    char in[32];
    char out[32];
    snprintf(in, sizeof in, "2021-%02d-28T10:20:30Z", mo);
    bitwriter_init(&bw);
    ck_assert_int_eq(dvb_datetime_encode(&bw, in), 0);
    bd = bitwriter_data(&bw, &bl);
    bitreader_init(&br, bd, bl);
    ck_assert_int_eq(dvb_datetime_decode(&br, out, sizeof out), 0);
    ck_assert_str_eq(out, in);
    bitwriter_free(&bw);
  }
}
END_TEST

static int run_ct(const fld_t *f, size_t nf, const char *href, size_t drop, char *out, size_t cap) {
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *bd, *sd;
  size_t bl, sl;
  int rc;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  put_fields(&bw, f, nf);
  ck_assert_int_eq(strrepo_writer_put(&sw, href), 0);
  bd = bitwriter_data(&bw, &bl);
  sd = strrepo_writer_data(&sw, &sl);
  ck_assert_uint_ge(bl, drop);
  bitreader_init(&br, bd, bl - drop);
  ck_assert_int_eq(strrepo_reader_init(&sr, sd, sl), 0);
  rc = dvb_controlledterm_decode(&br, &sr, out, cap);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

START_TEST(controlledterm_compact_forms) {
  fld_t ungrouped[] = {{1, 1}, {0, 1}, {5, 7}, {300, -8}};
  fld_t grouped[] = {{1, 1}, {1, 1}, {9, -4}, {200, -8}, {70000, -8}};
  char out[64];
  ck_assert_int_eq(run_ct(ungrouped, NELEM(ungrouped), "", 0, out, sizeof out), 0);
  ck_assert_str_eq(out, "dvbcs:0:5:300");
  ck_assert_int_eq(run_ct(grouped, NELEM(grouped), "", 0, out, sizeof out), 0);
  ck_assert_str_eq(out, "dvbcs:9:200:70000");
}
END_TEST

START_TEST(controlledterm_string_form_and_failures) {
  fld_t str[] = {{0, 1}};
  fld_t ungrouped[] = {{1, 1}, {0, 1}, {5, 7}, {3, -8}};
  fld_t grouped[] = {{1, 1}, {1, 1}, {9, -4}, {200, -8}, {7, -8}};
  char out[64];
  ck_assert_int_eq(run_ct(str, NELEM(str), "urn:x", 0, out, sizeof out), 0);
  ck_assert_str_eq(out, "urn:x");
  ck_assert_int_eq(run_ct(str, 0, "urn:x", 0, out, sizeof out), -1);
  ck_assert_int_eq(run_ct(ungrouped, NELEM(ungrouped), "", 0, out, 11), -1);
  ck_assert_int_eq(run_ct(ungrouped, NELEM(ungrouped), "", 0, out, 12), 0);
  for (size_t drop = 1; drop <= 2; drop++) ck_assert_int_eq(run_ct(ungrouped, NELEM(ungrouped), "", drop, out, sizeof out), -1);
  for (size_t drop = 1; drop <= 4; drop++) ck_assert_int_eq(run_ct(grouped, NELEM(grouped), "", drop, out, sizeof out), -1);
}
END_TEST

START_TEST(controlledterm_rejects_truncated_group_and_scheme) {
  fld_t g1[] = {{1, 1}, {1, 1}};
  fld_t g2[] = {{1, 1}, {1, 1}, {9, -4}};
  fld_t g3[] = {{1, 1}, {1, 1}, {9, -4}, {1, -8}};
  fld_t u1[] = {{1, 1}, {0, 1}};
  char out[32];
  ck_assert_int_eq(run_ct(g1, NELEM(g1), "", 1, out, sizeof out), -1);
  ck_assert_int_eq(run_ct(g2, NELEM(g2), "", 0, out, sizeof out), -1);
  ck_assert_int_eq(run_ct(g3, NELEM(g3), "", 0, out, sizeof out), -1);
  ck_assert_int_eq(run_ct(u1, NELEM(u1), "", 0, out, sizeof out), -1);
}
END_TEST

static Suite *dvbtypes_suite(void) {
  Suite *s = suite_create("dvbtypes");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, locator_optimized_full_form);
  tcase_add_test(tc, locator_optimized_variants);
  tcase_add_test(tc, locator_optimized_caps_content_tags_at_32);
  tcase_add_test(tc, locator_optimized_reuses_prefix_and_day);
  tcase_add_test(tc, locator_optimized_needs_prefix_and_day_state);
  tcase_add_test(tc, locator_optimized_rejects_reserved_event_kind);
  tcase_add_test(tc, locator_optimized_rejects_every_short_output_buffer);
  tcase_add_test(tc, locator_optimized_rejects_every_truncation);
  tcase_add_test(tc, locator_optimized_rejects_oversized_path);
  tcase_add_test(tc, locator_decode_without_state_and_string_branch);
  tcase_add_test(tc, string_decode_dup_handles_empty_and_failure);
  tcase_add_test(tc, datetime_published_form);
  tcase_add_test(tc, datetime_published_reuses_date);
  tcase_add_test(tc, datetime_rejects_bad_streams);
  tcase_add_test(tc, datetime_accepts_range_boundaries);
  tcase_add_test(tc, datetime_rejects_every_truncation);
  tcase_add_test(tc, datetime_rejects_short_output_buffer);
  tcase_add_test(tc, datetime_round_trips_every_month);
  tcase_add_test(tc, controlledterm_compact_forms);
  tcase_add_test(tc, controlledterm_string_form_and_failures);
  tcase_add_test(tc, controlledterm_rejects_truncated_group_and_scheme);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dvbtypes_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
