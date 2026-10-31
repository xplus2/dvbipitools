/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"
#include "../../fx_bim.h"

static void decode_all(const bim_simple_t *s, unsigned compat, const unsigned char *bits, size_t n, const unsigned char *strs, size_t slen) {
  bim_decode_opts_t o;
  bitreader_t br;
  strrepo_reader_t sr;
  char *out = NULL;
  memset(&o, 0, sizeof o);
  o.mpegb = 1;
  o.compat = compat;
  o.inflate = ident;
  o.lax_schema = -1;
  bitreader_init(&br, bits, n);
  ck_assert_int_eq(strrepo_reader_init(&sr, strs, slen), 0);
  if (!bim_decode_simple(s, &o, &br, &sr, &out)) free(out);
  bitreader_init(&br, bits, n);
  if (!bim_decode_simple(s, &o, &br, NULL, &out)) free(out);
  o.inflate = NULL;
  bitreader_init(&br, bits, n);
  if (!bim_decode_simple(s, &o, &br, NULL, &out)) free(out);
}

static void sweep_value(const bim_simple_t *s, const char *v, unsigned compat) {
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *p;
  const unsigned char *q;
  unsigned char *copy;
  size_t n;
  size_t qn;
  memset(&eo, 0, sizeof eo);
  eo.mpegb = 1;
  eo.compat = compat;
  eo.deflate = ident;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (bim_encode_simple(s, v, &eo, &bw, &sw)) {
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    return;
  }
  p = bitwriter_data(&bw, &n);
  q = strrepo_writer_data(&sw, &qn);
  copy = malloc(n ? n : 1);
  ck_assert_ptr_nonnull(copy);
  if (n) memcpy(copy, p, n);
  decode_all(s, compat, copy, n, q, qn);
  for (size_t cut = 0; cut < n; cut++) decode_all(s, compat, copy, cut, q, qn);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    decode_all(s, compat, copy, n, q, qn);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  free(copy);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}

static bim_simple_t simple(bim_codec_t c) {
  bim_simple_t s;
  memset(&s, 0, sizeof s);
  s.codec = c;
  return s;
}

START_TEST(decode_simple_primitive_codecs) {
  static const char *const texts[] = {"", "abc", "true", "false", "1.5", "NaN", "INF", "-INF", "-0", "00ff", "AQID", "5", "-5", "0", "a", "b", "2030-01-01T12:00:00Z", "PT1H30M",
                                      "P1D", "2024-05-01T10:00:00Z", "2024-05-01", "dvb://233a.1.1", "urn:x:y", "9007199254740993", "3.14159265358979"};
  static const bim_codec_t codecs[] = {BIM_UTF8,     BIM_BOOLEAN,   BIM_FLOAT,   BIM_DOUBLE,  BIM_HEXBIN,   BIM_BASE64,       BIM_SVLU5, BIM_STRING,
                                       BIM_ZSTRING,  BIM_LOCATOR,   BIM_DATETIME, BIM_PUBTIME, BIM_DURATION, BIM_TERMREF,      BIM_TVA_DATETIME, BIM_TVA_DATE};
  for (size_t c = 0; c < sizeof codecs / sizeof *codecs; c++) {
    bim_simple_t s = simple(codecs[c]);
    for (size_t i = 0; i < sizeof texts / sizeof *texts; i++) sweep_value(&s, texts[i], 0);
  }
}
END_TEST

START_TEST(decode_simple_structured_codecs) {
  static const char *const names[] = {"a", "b", "c"};
  bim_simple_t range = simple(BIM_RANGE);
  bim_simple_t wide = simple(BIM_RANGE);
  bim_simple_t neg = simple(BIM_RANGE);
  bim_simple_t en = simple(BIM_ENUM);
  bim_simple_t list = simple(BIM_LIST);
  bim_simple_t ulist = simple(BIM_LIST);
  bim_simple_t un = simple(BIM_UNION);
  bim_simple_t utf = simple(BIM_UTF8);
  const bim_simple_t *members[2];
  static const char *const values[] = {"0", "3", "7", "-3", "10", "a b", "1 2 3", "1 2", "", "5 5 5 5", "b", "x", "18446744073709551615", "-9223372036854775808"};
  range.range.min = 2;
  range.range.span = 9;
  wide.range.min = 0;
  wide.range.span = UINT64_MAX;
  neg.range.min = -4;
  neg.range.span = 10;
  en.enums = names;
  en.nenums = 3;
  list.item = &range;
  list.range.min = 1;
  list.range.span = 3;
  ulist.item = &range;
  ulist.unbounded = 1;
  members[0] = &range;
  members[1] = &utf;
  un.members = members;
  un.nmembers = 2;
  for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
    sweep_value(&range, values[i], 0);
    sweep_value(&wide, values[i], 0);
    sweep_value(&neg, values[i], 0);
    sweep_value(&en, values[i], 0);
    sweep_value(&list, values[i], 0);
    sweep_value(&ulist, values[i], 0);
    sweep_value(&un, values[i], 0);
  }
}
END_TEST

START_TEST(decode_simple_mpeg7_time_codecs) {
  static const char *const tps[] = {"", "2030", "-2030-12-31", "T12:30:45:3.99F9+01:00", "2030-12-31T23:59:59F1+05:30", "T12", "F7", "+16:00", "2030-12", "T12:30"};
  static const char *const dus[] = {"P", "-P1DT5H30M20S3N07f9F+01:00Z", "PT1H", "P1DT23H59M59S3N07f9F", "PT5N", "PT1S5N", "P1F", "PT99f", "-P5D", "PT30M"};
  static const char all_tp[] = "|-1|1|1-01|1-01-01|T00|T00:00|T00:00:00|T00:00:00:1|T00:00:00:1.00|F1|+01:00|";
  static const char partial[] = "|1|1-01|1-01-01|T00|T00:00|";
  static const void *const p_all[] = {all_tp};
  static const void *const p_part[] = {partial};
  bim_simple_t tp = simple(BIM_M7_TIMEPOINT);
  bim_simple_t du = simple(BIM_M7_DURATION);
  bim_simple_t tp2 = simple(BIM_M7_TIMEPOINT);
  tp2.patterns = p_part;
  tp2.npatterns = 1;
  tp2.match = NULL;
  (void)p_all;
  for (size_t i = 0; i < sizeof tps / sizeof tps[0]; i++)
    for (unsigned compat = 0; compat < 2; compat++) {
      unsigned flags = compat ? BIM_COMPAT_M7_FLAGS : 0;
      sweep_value(&tp, tps[i], flags);
      sweep_value(&tp2, tps[i], flags);
    }
  for (size_t i = 0; i < sizeof dus / sizeof dus[0]; i++)
    for (unsigned compat = 0; compat < 2; compat++) sweep_value(&du, dus[i], compat ? BIM_COMPAT_M7_FLAGS : 0);
}
END_TEST

START_TEST(decode_simple_unknown_codec_and_null) {
  bim_simple_t s = simple((bim_codec_t)99);
  bim_decode_opts_t o;
  bitreader_t br;
  unsigned char zero[8] = {0};
  char *out = NULL;
  memset(&o, 0, sizeof o);
  bitreader_init(&br, zero, sizeof zero);
  ck_assert_int_eq(bim_decode_simple(&s, &o, &br, NULL, &out), -1);
  ck_assert_int_eq(bim_decode_simple(NULL, &o, &br, NULL, &out), -1);
  ck_assert_int_eq(bim_decode_simple(&s, NULL, &br, NULL, &out), -1);
}
END_TEST

START_TEST(decode_simple_allocation_failures) {
  bim_simple_t s = simple(BIM_UTF8);
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *p;
  size_t n;
  memset(&eo, 0, sizeof eo);
  eo.mpegb = 1;
  eo.lax_schema = -1;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_simple(&s, "hello", &eo, &bw, &sw), 0);
  p = bitwriter_data(&bw, &n);
  for (long k = 0;; k++) {
    bim_decode_opts_t o;
    bitreader_t br;
    char *out = NULL;
    int hit;
    int rc;
    memset(&o, 0, sizeof o);
    o.mpegb = 1;
    bitreader_init(&br, p, n);
    alloc_fail_at(k);
    rc = bim_decode_simple(&s, &o, &br, NULL, &out);
    hit = alloc_fail_end();
    if (!rc) free(out);
    if (!hit) break;
  }
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static int match_prefix_ok(const void *pattern, const char *v) {
  return !strncmp(v, pattern, strlen(pattern));
}

static int encode_value(const bim_simple_t *s, const char *v, unsigned compat) {
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc;
  memset(&eo, 0, sizeof eo);
  eo.mpegb = 1;
  eo.compat = compat;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  rc = bim_encode_simple(s, v, &eo, &bw, &sw);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

#define OK(s, v) ck_assert_msg(encode_value(&(s), v, 0) == 0, "rejected '%s'", v)
#define BAD(s, v) ck_assert_msg(encode_value(&(s), v, 0) == -1, "accepted '%s'", v)

START_TEST(encode_simple_lexical_rules) {
  bim_simple_t d = simple(BIM_DOUBLE);
  bim_simple_t f = simple(BIM_FLOAT);
  bim_simple_t b = simple(BIM_BOOLEAN);
  bim_simple_t h = simple(BIM_HEXBIN);
  bim_simple_t b64 = simple(BIM_BASE64);
  bim_simple_t sv = simple(BIM_SVLU5);
  bim_simple_t r = simple(BIM_RANGE);
  bim_simple_t rn = simple(BIM_RANGE);
  bim_simple_t rw = simple(BIM_RANGE);
  static const char *const dok[] = {"1", " 1 ", "+1", "-1", ".5", "5.", "+.5", "1e5", "1E+5", "1e-5", "1.5e3", "INF", "-INF", "NaN", " NaN ", "0", "-0.0"};
  static const char *const dbad[] = {"", " ", ".", "+", "e5", "1e", "1e+", "1e5x", "inf", "nan", "NAN", "+INF", "1 2", "0x10", "--1", "1.2.3"};
  static const char *const bok[] = {"true", "false", "1", "0", " true ", "\t0\n"};
  static const char *const bbad[] = {"", "TRUE", "yes", "2", "01", "tru", "truee"};
  static const char *const hok[] = {"", "00", "aB", "0aFf10", " 00ff "};
  static const char *const hbad[] = {"0", "0g", "g0", "000", "zz"};
  static const char *const svok[] = {"0", "-0", "+0", "7", "-7", " 12 ", "18446744073709551615"};
  static const char *const svbad[] = {"", "x", "-", "+", "1x", "--1", "18446744073709551616", "1.5", "- 1"};
  r.range.min = 5;
  r.range.span = 10;
  rn.range.min = -3;
  rn.range.span = 6;
  rw.range.min = -9223372036854775807LL - 1;
  rw.range.span = UINT64_MAX;
  for (size_t i = 0; i < sizeof dok / sizeof *dok; i++) {
    OK(d, dok[i]);
    OK(f, dok[i]);
  }
  for (size_t i = 0; i < sizeof dbad / sizeof *dbad; i++) {
    BAD(d, dbad[i]);
    BAD(f, dbad[i]);
  }
  for (size_t i = 0; i < sizeof bok / sizeof *bok; i++) OK(b, bok[i]);
  for (size_t i = 0; i < sizeof bbad / sizeof *bbad; i++) BAD(b, bbad[i]);
  for (size_t i = 0; i < sizeof hok / sizeof *hok; i++) OK(h, hok[i]);
  for (size_t i = 0; i < sizeof hbad / sizeof *hbad; i++) BAD(h, hbad[i]);
  for (size_t i = 0; i < sizeof svok / sizeof *svok; i++) OK(sv, svok[i]);
  for (size_t i = 0; i < sizeof svbad / sizeof *svbad; i++) BAD(sv, svbad[i]);
  OK(b64, "AQID");
  OK(b64, "");
  BAD(b64, "!!!!");
  OK(r, "5");
  OK(r, "15");
  OK(r, " 7 ");
  OK(r, "+7");
  BAD(r, "4");
  BAD(r, "16");
  BAD(r, "-1");
  BAD(r, "-0x");
  BAD(r, "-0");
  OK(rn, "-3");
  OK(rn, "3");
  OK(rn, "0");
  BAD(rn, "-4");
  BAD(rn, "4");
  OK(rw, "-9223372036854775808");
  OK(rw, "9223372036854775807");
  BAD(rw, "18446744073709551615");
  BAD(rw, "-9223372036854775809");
  BAD(rw, "18446744073709551616");
  {
    char big[2200];
    memset(big, 'a', sizeof big - 1);
    big[sizeof big - 1] = 0;
    BAD(h, big);
    BAD(b64, big);
    big[1000] = 0;
    OK(h, "00");
    (void)big;
  }
}
END_TEST

START_TEST(encode_simple_facets_lists_and_unions) {
  static const char pat_ok[] = "ok";
  static const void *const pats[] = {pat_ok};
  bim_simple_t item = simple(BIM_RANGE);
  bim_simple_t lst = simple(BIM_LIST);
  bim_simple_t ulst = simple(BIM_LIST);
  bim_simple_t unfac = simple(BIM_UNION);
  bim_simple_t u8 = simple(BIM_UTF8);
  bim_simple_t hx = simple(BIM_HEXBIN);
  bim_simple_t b64 = simple(BIM_BASE64);
  bim_simple_t en = simple(BIM_ENUM);
  bim_simple_t emp = simple(BIM_UNION);
  const bim_simple_t *members[3];
  const char *const names[] = {"ok1", "ok2"};
  char longitem[2200];
  item.range.min = 0;
  item.range.span = 9;
  lst.item = &item;
  lst.range.min = 2;
  lst.range.span = 3;
  ulst.item = &item;
  ulst.unbounded = 1;
  u8.match = match_prefix_ok;
  u8.patterns = pats;
  u8.npatterns = 1;
  u8.has_length = 1;
  u8.minlen = 2;
  u8.maxlen = 4;
  hx.has_length = 1;
  hx.minlen = 1;
  hx.maxlen = 2;
  b64.has_length = 1;
  b64.minlen = 3;
  b64.maxlen = 3;
  en.enums = names;
  en.nenums = 2;
  members[0] = &u8;
  members[1] = &hx;
  members[2] = &en;
  unfac.members = members;
  unfac.nmembers = 3;
  OK(lst, "1 2");
  OK(lst, " 1 2 3 4 ");
  OK(lst, "1\t2\n3");
  BAD(lst, "1");
  BAD(lst, "");
  BAD(lst, "1 2 3 4 5 6");
  BAD(lst, "1 x");
  OK(ulst, "");
  OK(ulst, "1 2 3 4 5 6 7 8");
  BAD(ulst, "1 22");
  memset(longitem, '1', sizeof longitem - 1);
  longitem[sizeof longitem - 1] = 0;
  BAD(ulst, longitem);
  OK(u8, "ok");
  OK(hx, "00");
  OK(b64, "AQID");
  for (int m = 0; m < 3; m++) {
    bim_simple_t one = simple(BIM_UNION);
    const bim_simple_t *only[1];
    only[0] = members[m];
    one.members = only;
    one.nmembers = 1;
    if (m == 0) {
      OK(one, "ok");
      OK(one, "okay");
      OK(one, "ok\xc3\xa4");
      BAD(one, "o");
      BAD(one, "okaaay");
      BAD(one, "no");
    } else if (m == 1) {
      OK(one, "00");
      OK(one, "0011");
      BAD(one, "001122");
      BAD(one, "");
    } else {
      OK(one, "ok1");
      BAD(one, "ok3");
    }
  }
  {
    bim_simple_t bu = simple(BIM_UNION);
    const bim_simple_t *bm[1];
    bim_simple_t b64l = simple(BIM_BASE64);
    b64l.has_length = 1;
    b64l.minlen = 3;
    b64l.maxlen = 3;
    bm[0] = &b64l;
    bu.members = bm;
    bu.nmembers = 1;
    OK(bu, "AQID");
    BAD(bu, "AQ==");
    BAD(bu, "!!!!");
  }
  OK(en, "ok1");
  OK(en, "ok2");
  BAD(en, "ok3");
  OK(unfac, "okay");
  OK(unfac, "00");
  OK(unfac, "ok1");
  BAD(unfac, "zzz");
  BAD(unfac, "");
  BAD(emp, "x");
}
END_TEST

START_TEST(encode_simple_survives_allocation_failures) {
  static const char pat_ok[] = "ok";
  static const void *const pats[] = {pat_ok};
  bim_simple_t item = simple(BIM_RANGE);
  bim_simple_t lst = simple(BIM_LIST);
  bim_simple_t u8 = simple(BIM_UTF8);
  bim_simple_t un = simple(BIM_UNION);
  bim_simple_t st = simple(BIM_STRING);
  bim_simple_t zs = simple(BIM_ZSTRING);
  const bim_simple_t *members[2];
  const bim_simple_t *all[] = {&lst, &un, &st, &zs, &u8};
  const char *const vals[] = {"1 2 3", "okay", "hello world, a longer string value", "hello world, a longer string value", "ok"};
  item.range.span = 9;
  lst.item = &item;
  lst.range.min = 1;
  lst.range.span = 5;
  u8.match = match_prefix_ok;
  u8.patterns = pats;
  u8.npatterns = 1;
  members[0] = &u8;
  members[1] = &st;
  un.members = members;
  un.nmembers = 2;
  for (size_t i = 0; i < sizeof all / sizeof all[0]; i++)
    for (long n = 0;; n++) {
      bim_encode_opts_t eo;
      bitwriter_t bw;
      strrepo_writer_t sw;
      int hit;
      memset(&eo, 0, sizeof eo);
      eo.mpegb = 1;
      eo.lax_schema = -1;
      bitwriter_init(&bw);
      ck_assert_int_eq(strrepo_writer_init(&sw), 0);
      for (int k = 0; k < n % 40; k++) ck_assert_int_eq(bitwriter_put(&bw, 0xA5, 8), 0);
      alloc_fail_at(n / 40);
      (void)bim_encode_simple(all[i], vals[i], &eo, &bw, &sw);
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      if (!hit && n % 40 == 39) break;
    }
}
END_TEST

START_TEST(simple_values_beyond_4k_round_trip) {
  static const bim_simple_t utf8 = {.codec = BIM_UTF8};
  static const bim_simple_t str = {.codec = BIM_STRING};
  static const bim_simple_t hex = {.codec = BIM_HEXBIN};
  const bim_simple_t *s[] = {&utf8, &str, &hex};
  char *v = malloc(20001);
  ck_assert_ptr_nonnull(v);
  for (size_t i = 0; i < 20000; i++) v[i] = "0123456789ABCDEF"[i % 16];
  v[20000] = '\0';
  for (size_t k = 0; k < sizeof s / sizeof s[0]; k++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    bitreader_t br;
    strrepo_reader_t sr;
    const unsigned char *b;
    const unsigned char *r;
    size_t bl;
    size_t rl;
    char *out = NULL;
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    ck_assert_int_eq(bim_encode_simple(s[k], v, NULL, &bw, &sw), 0);
    b = bitwriter_data(&bw, &bl);
    r = strrepo_writer_data(&sw, &rl);
    bitreader_init(&br, b, bl);
    ck_assert_int_eq(strrepo_reader_init(&sr, r, rl), 0);
    ck_assert_int_eq(bim_decode_simple(s[k], NULL, &br, &sr, &out), 0);
    ck_assert_str_eq(out, v);
    free(out);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
  free(v);
}
END_TEST

START_TEST(coding_widths_and_subtype_lookup) {
  bim_part_t p;
  bim_range_t r;
  int shunt = 9;
  bim_type_t base = {0};
  bim_type_t same_name = {0};
  bim_type_t other = {0};
  bim_type_t anon = {0};
  bim_subtypes_t subs[1];
  bim_schema_t sc;
  memset(&p, 0, sizeof p);
  p.min = 0;
  p.max = 1;
  ck_assert_int_eq(occ_coding(&p, &shunt), 0);
  ck_assert_int_eq(shunt, 1);
  p.min = 1;
  ck_assert_int_eq(occ_coding(&p, &shunt), 0);
  ck_assert_int_eq(shunt, 0);
  p.min = 0;
  p.max = 4;
  ck_assert_int_eq(occ_coding(&p, &shunt), 3);
  ck_assert_int_eq(shunt, 1);
  p.max = BIM_UNBOUNDED;
  ck_assert_int_eq(occ_coding(&p, &shunt), -1);
  p.max = 70000;
  ck_assert_int_eq(occ_coding(&p, &shunt), -1);
  p.min = 2;
  p.max = 2;
  ck_assert_int_eq(occ_coding(&p, &shunt), 0);
  memset(&r, 0, sizeof r);
  r.span = 255;
  ck_assert_int_eq(range_bits(&r), 8);
  r.span = 65535;
  ck_assert_int_eq(range_bits(&r), 16);
  r.span = 65536;
  ck_assert_int_eq(range_bits(&r), -1);
  base.name = "T";
  base.ns = "urn:a";
  same_name.name = "T";
  same_name.ns = "urn:a";
  other.name = "T";
  other.ns = "urn:b";
  anon.name = NULL;
  memset(subs, 0, sizeof subs);
  subs[0].base = &base;
  memset(&sc, 0, sizeof sc);
  sc.subs = subs;
  sc.nsubs = 1;
  ck_assert_ptr_eq(bim_subtypes_find(&sc, &base), &subs[0]);
  ck_assert_ptr_null(bim_subtypes_find(&sc, &same_name));
  ck_assert_ptr_eq(bim_subtypes_find_x(&sc, &base), &subs[0]);
  ck_assert_ptr_eq(bim_subtypes_find_x(&sc, &same_name), &subs[0]);
  ck_assert_ptr_null(bim_subtypes_find_x(&sc, &other));
  ck_assert_ptr_null(bim_subtypes_find_x(&sc, &anon));
  ck_assert_ptr_null(bim_subtypes_find_x(&sc, NULL));
  same_name.ns = NULL;
  base.ns = NULL;
  ck_assert_ptr_eq(bim_subtypes_find_x(&sc, &same_name), &subs[0]);
  base.ns = "urn:a";
  ck_assert_ptr_null(bim_subtypes_find_x(&sc, &same_name));
}
END_TEST

static Suite *decode_simple_suite(void) {
  Suite *s = suite_create("bim_decode_simple");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, decode_simple_primitive_codecs);
  tcase_add_test(tc, decode_simple_structured_codecs);
  tcase_add_test(tc, decode_simple_mpeg7_time_codecs);
  tcase_add_test(tc, decode_simple_unknown_codec_and_null);
  tcase_add_test(tc, decode_simple_allocation_failures);
  tcase_add_test(tc, encode_simple_lexical_rules);
  tcase_add_test(tc, encode_simple_facets_lists_and_unions);
  tcase_add_test(tc, encode_simple_survives_allocation_failures);
  tcase_add_test(tc, simple_values_beyond_4k_round_trip);
  tcase_add_test(tc, coding_widths_and_subtype_lookup);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(decode_simple_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
