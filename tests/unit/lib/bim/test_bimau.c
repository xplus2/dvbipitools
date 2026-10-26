/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/bimau.h"
#include "../../cfg_fixture.h"

static const unsigned char schema_au[] = {0x01, 0x02, 0xA0, 0xB0};

static uint64_t *u64s(size_t n, const uint64_t *v) {
  uint64_t *p = calloc(n, sizeof *p);
  ck_assert_ptr_nonnull(p);
  memcpy(p, v, n * sizeof *p);
  return p;
}

static void init_di(bim_decoderinit_t *di, bim_di_schema_t *sch) {
  memset(di, 0, sizeof *di);
  memset(sch, 0, sizeof *sch);
  sch->uri = "urn:a";
  di->mpegb = 1;
  di->add_schema = 1;
  di->hier = 1;
  di->nadditional = 4;
  di->schemas = sch;
  di->nschemas = 1;
}

static void set_encoded(bim_bits_t *b) {
  b->heap_bits = malloc(sizeof schema_au);
  memcpy(b->heap_bits, schema_au, sizeof schema_au);
  b->bits = b->heap_bits;
  b->nbits = sizeof schema_au * 8;
}

static void build_au(bim_au_t *au) {
  static const uint64_t c3[] = {0, 1, 2};
  static const uint64_t one[] = {1};
  static const uint64_t four[] = {4};
  static const uint64_t two[] = {2};
  static const uint64_t seven[] = {7};
  static const uint64_t c2[] = {5, 6};
  bim_suu_t *s;
  memset(au, 0, sizeof *au);
  au->nsuus = 2;
  au->suus = calloc(2, sizeof *au->suus);
  s = &au->suus[0];
  s->schema = 1;
  s->first = 1;
  s->ns = dup_checked("urn:x");
  s->nimported = 1;
  s->imported = calloc(1, sizeof *s->imported);
  s->imported[0] = dup_checked("urn:a");
  s->types.nglobal = 3;
  s->types.ext.all = 1;
  s->types.ext.present = s->types.ext.all;
  s->types.n = 3;
  s->types.counts = u64s(3, c3);
  s->elems.nglobal = 2;
  s->elems.partial = 1;
  s->elems.n = 1;
  s->elems.codes = u64s(1, one);
  s->elems.counts = u64s(1, four);
  s->attrs.nglobal = 5;
  s->attrs.partial = 1;
  s->attrs.n = 1;
  s->attrs.codes = u64s(1, four);
  set_encoded(&s->encoded);
  s = &au->suus[1];
  s->schema = 1;
  s->types.partial = 1;
  s->types.n = 1;
  s->types.codes = u64s(1, two);
  s->types.counts = u64s(1, seven);
  s->elems.n = 2;
  s->elems.counts = u64s(2, c2);
  set_encoded(&s->encoded);
  au->nfuus = 1;
  au->fuus = calloc(1, sizeof *au->fuus);
  au->fuus[0].heap_bits = malloc(2);
  au->fuus[0].heap_bits[0] = 0x12;
  au->fuus[0].heap_bits[1] = 0x34;
  au->fuus[0].bits = au->fuus[0].heap_bits;
  au->fuus[0].nbits = 16;
}

START_TEST(parses_15938_fuus) {
  static const unsigned char buf[] = {0x02, 0x02, 0xAA, 0xBB, 0x01, 0xCC};
  bim_decoderinit_t di;
  bim_au_state_t st;
  bim_au_t au;
  bim_err_t err;
  size_t used;
  memset(&di, 0, sizeof di);
  ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
  ck_assert_int_eq(bim_au_parse(&st, buf, sizeof buf, &au, &used, &err), 0);
  ck_assert_uint_eq(used, sizeof buf);
  ck_assert_uint_eq(au.nfuus, 2);
  ck_assert_uint_eq(au.fuus[0].nbits, 16);
  ck_assert_uint_eq(au.fuus[0].bits[1], 0xBB);
  ck_assert_uint_eq(au.fuus[1].bits[0], 0xCC);
  bim_au_free(&au);
  ck_assert_int_eq(bim_au_parse(&st, buf, sizeof buf - 1, &au, &used, &err), -1);
  ck_assert_str_eq(err.code, "LE400299");
  bim_au_state_free(&st);
}
END_TEST

START_TEST(writes_and_parses_suus) {
  bim_decoderinit_t di;
  bim_di_schema_t sch;
  bim_au_state_t wst;
  bim_au_state_t rst;
  bim_au_t in;
  bim_au_t out;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  size_t used;
  init_di(&di, &sch);
  build_au(&in);
  ck_assert_int_eq(bim_au_state_init(&wst, &di), 0);
  ck_assert_int_eq(bim_au_state_init(&rst, &di), 0);
  bitwriter_init(&bw);
  if (bim_au_write(&wst, &in, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  if (bim_au_parse(&rst, p, n, &out, &used, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_uint_eq(used, n);
  ck_assert_uint_eq(out.nsuus, 2);
  ck_assert_str_eq(out.suus[0].ns, "urn:x");
  ck_assert_str_eq(out.suus[0].imported[0], "urn:a");
  ck_assert_int_eq(out.suus[0].types.ext.all, 1);
  ck_assert_uint_eq(out.suus[0].types.counts[2], 2);
  ck_assert_uint_eq(out.suus[0].elems.codes[0], 1);
  ck_assert_uint_eq(out.suus[0].attrs.codes[0], 4);
  ck_assert_uint_eq(out.suus[0].encoded.nbits, 32);
  ck_assert_uint_eq(out.suus[1].types.codes[0], 2);
  ck_assert_uint_eq(out.suus[1].types.counts[0], 7);
  ck_assert_uint_eq(out.suus[1].elems.counts[1], 6);
  ck_assert_uint_eq(out.suus[1].encoded.bits[3], 0xB0);
  ck_assert_uint_eq(out.fuus[0].bits[1], 0x34);
  ck_assert_str_eq(rst.add[1].ns, "urn:x");
  ck_assert_uint_eq(rst.add[1].nattrs, 5);
  bim_au_free(&out);
  bim_au_free(&in);
  bitwriter_free(&bw);
  bim_au_state_free(&wst);
  bim_au_state_free(&rst);
}
END_TEST

START_TEST(refuses_invalid_suus) {
  bim_decoderinit_t di;
  bim_di_schema_t sch;
  bim_au_state_t st;
  bim_au_t in;
  bim_au_t out;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  size_t used;
  bim_bits_t keep;
  init_di(&di, &sch);
  build_au(&in);
  ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
  bitwriter_init(&bw);
  /* second SUU alone: first SUU missing */
  in.nsuus = 1;
  in.suus[0].first = 0;
  ck_assert_int_eq(bim_au_write(&st, &in, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400301");
  in.suus[0].first = 1;
  in.suus[0].schema = 0;
  ck_assert_int_eq(bim_au_write(&st, &in, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400300");
  in.suus[0].schema = 1;
  keep = in.suus[0].encoded;
  in.suus[0].encoded.heap_bits[0] = 0x02;
  bitwriter_reset(&bw);
  ck_assert_int_eq(bim_au_write(&st, &in, &bw, &err), 0);
  in.suus[0].encoded = keep;
  bim_au_state_free(&st);
  ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_int_eq(bim_au_parse(&st, p, n, &out, &used, &err), -1);
  ck_assert_str_eq(err.code, "LE400302");
  in.nsuus = 2;
  bim_au_free(&in);
  bitwriter_free(&bw);
  bim_au_state_free(&st);
}
END_TEST

static void write_au(const bim_decoderinit_t *di, const bim_au_t *au, bitwriter_t *bw, int expect) {
  bim_au_state_t st;
  bim_err_t err;
  ck_assert_int_eq(bim_au_state_init(&st, di), 0);
  bitwriter_init(bw);
  ck_assert_int_eq(bim_au_write(&st, au, bw, &err), expect);
  bim_au_state_free(&st);
}

typedef void (*au_mut)(const bim_au_t *au);

static void a_count_mismatch(const bim_au_t *au) { au->suus[0].types.n = 2; }
static void a_code_range(const bim_au_t *au) { au->suus[0].elems.codes[0] = 9; }
static void a_attr_range(const bim_au_t *au) { au->suus[0].attrs.codes[0] = 9; }
static void a_empty_ns(const bim_au_t *au) { au->suus[0].ns[0] = 0; }
static void a_empty_import(const bim_au_t *au) { au->suus[0].imported[0][0] = 0; }
static void a_fuu_bits(const bim_au_t *au) { au->fuus[0].nbits = 13; }
static void a_update_count(const bim_au_t *au) { au->suus[1].types.partial = 0; au->suus[1].types.n = 5; }
static void a_nonpartial_elems_range(const bim_au_t *au) { au->suus[1].elems.n = 1; }
static void a_partial_types_range(const bim_au_t *au) { au->suus[1].types.codes[0] = 99; }
static void a_ns_taken(const bim_au_t *au) {
  free(au->suus[0].ns);
  au->suus[0].ns = dup_checked("urn:a");
}

START_TEST(bimau_write_rejects_invalid_units) {
  static const au_mut muts[] = {a_count_mismatch, a_code_range, a_attr_range, a_empty_ns, a_empty_import, a_fuu_bits, a_update_count, a_nonpartial_elems_range, a_partial_types_range, a_ns_taken};
  for (size_t k = 0; k < sizeof muts / sizeof *muts; k++) {
    bim_decoderinit_t di;
    bim_di_schema_t sch;
    bim_au_t au;
    bitwriter_t bw;
    init_di(&di, &sch);
    build_au(&au);
    muts[k](&au);
    write_au(&di, &au, &bw, -1);
    bitwriter_free(&bw);
    bim_au_free(&au);
  }
  {
    bim_decoderinit_t di;
    bim_di_schema_t sch;
    bim_au_t au;
    bitwriter_t bw;
    init_di(&di, &sch);
    di.add_schema = 0;
    build_au(&au);
    write_au(&di, &au, &bw, -1);
    bitwriter_free(&bw);
    au.nsuus = 0;
    di.add_schema_only = 0;
    write_au(&di, &au, &bw, 0);
    bitwriter_free(&bw);
    di.add_schema = 1;
    di.add_schema_only = 1;
    au.nsuus = 2;
    write_au(&di, &au, &bw, -1);
    bitwriter_free(&bw);
    au.nfuus = 0;
    write_au(&di, &au, &bw, 0);
    bitwriter_free(&bw);
    au.nsuus = 2;
    au.nfuus = 1;
    bim_au_free(&au);
  }
}
END_TEST

START_TEST(bimau_parse_state_across_units) {
  bim_decoderinit_t di;
  bim_decoderinit_t other;
  bim_di_schema_t sch;
  bim_di_schema_t osch;
  bim_au_t one;
  bim_au_t two;
  bim_au_t out;
  bim_au_state_t rst;
  bim_err_t err;
  bitwriter_t b1;
  bitwriter_t b2;
  bitwriter_t b3;
  const unsigned char *p;
  size_t n;
  size_t used;
  init_di(&di, &sch);
  build_au(&one);
  write_au(&di, &one, &b1, 0);
  build_au(&two);
  free(two.suus[0].ns);
  two.suus[0].ns = dup_checked("urn:changed");
  write_au(&di, &two, &b2, 0);
  ck_assert_int_eq(bim_au_state_init(&rst, &di), 0);
  p = bitwriter_data(&b1, &n);
  ck_assert_int_eq(bim_au_parse(&rst, p, n, &out, &used, &err), 0);
  bim_au_free(&out);
  p = bitwriter_data(&b1, &n);
  ck_assert_int_eq(bim_au_parse(&rst, p, n, &out, &used, &err), 0);
  bim_au_free(&out);
  p = bitwriter_data(&b2, &n);
  ck_assert_int_eq(bim_au_parse(&rst, p, n, &out, &used, &err), -1);
  ck_assert_str_eq(err.code, "LE400300");
  bim_au_state_free(&rst);

  ck_assert_int_eq(bim_au_state_init(&rst, &di), 0);
  one.suus[0].first = 0;
  one.nsuus = 1;
  other = di;
  osch = sch;
  other.schemas = &osch;
  osch.uri = "urn:x";
  bitwriter_init(&b3);
  {
    bim_au_t only = one;
    bim_au_state_t wst;
    ck_assert_int_eq(bim_au_state_init(&wst, &other), 0);
    wst.add[1].ns = dup_checked("urn:w");
    wst.add[1].ntypes = 3;
    wst.add[1].nelems = 2;
    wst.add[1].nattrs = 5;
    ck_assert_int_eq(bim_au_write(&wst, &only, &b3, &err), 0);
    bim_au_state_free(&wst);
  }
  p = bitwriter_data(&b3, &n);
  ck_assert_int_eq(bim_au_parse(&rst, p, n, &out, &used, &err), -1);
  ck_assert_str_eq(err.code, "LE400301");
  bim_au_state_free(&rst);

  ck_assert_int_eq(bim_au_state_init(&rst, &other), 0);
  p = bitwriter_data(&b1, &n);
  ck_assert_int_eq(bim_au_parse(&rst, p, n, &out, &used, &err), -1);
  ck_assert_str_eq(err.code, "LE400300");
  bim_au_state_free(&rst);

  bitwriter_free(&b1);
  bitwriter_free(&b2);
  bitwriter_free(&b3);
  one.nsuus = 2;
  bim_au_free(&one);
  bim_au_free(&two);
}
END_TEST

START_TEST(bimau_parse_sweeps_and_allocation_failures) {
  bim_decoderinit_t di;
  bim_di_schema_t sch;
  bim_au_t au;
  bitwriter_t bw;
  const unsigned char *p;
  unsigned char *copy;
  size_t n;
  init_di(&di, &sch);
  build_au(&au);
  write_au(&di, &au, &bw, 0);
  p = bitwriter_data(&bw, &n);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  for (size_t cut = 0; cut <= n; cut++)
    for (int mode = 0; mode < 2; mode++) {
      bim_au_state_t st;
      bim_au_t out;
      bim_err_t err;
      size_t used;
      di.add_schema_only = mode;
      ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
      if (!bim_au_parse(&st, copy, cut, &out, &used, &err)) bim_au_free(&out);
      bim_au_state_free(&st);
    }
  di.add_schema_only = 0;
  for (size_t bit = 0; bit < n * 8; bit++) {
    bim_au_state_t st;
    bim_au_t out;
    bim_err_t err;
    size_t used;
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
    if (!bim_au_parse(&st, copy, n, &out, &used, &err)) bim_au_free(&out);
    bim_au_state_free(&st);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  for (long k = 0;; k++) {
    bim_au_state_t st;
    bim_au_t out;
    bim_err_t err;
    size_t used;
    int hit;
    int rc;
    ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
    alloc_fail_at(k);
    rc = bim_au_parse(&st, copy, n, &out, &used, &err);
    hit = alloc_fail_end();
    if (!rc) bim_au_free(&out);
    bim_au_state_free(&st);
    if (!hit) break;
  }
  for (long k = 0;; k++) {
    bim_au_state_t st;
    bim_err_t err;
    bitwriter_t w2;
    int hit;
    ck_assert_int_eq(bim_au_state_init(&st, &di), 0);
    bitwriter_init(&w2);
    alloc_fail_at(k);
    (void)bim_au_write(&st, &au, &w2, &err);
    hit = alloc_fail_end();
    bitwriter_free(&w2);
    bim_au_state_free(&st);
    if (!hit) break;
  }
  {
    bim_au_state_t st;
    alloc_fail_at(0);
    (void)bim_au_state_init(&st, &di);
    alloc_fail_end();
    bim_au_state_free(&st);
  }
  free(copy);
  bitwriter_free(&bw);
  bim_au_free(&au);
}
END_TEST

static Suite *bimau_suite(void) {
  Suite *s = suite_create("bim_au");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parses_15938_fuus);
  tcase_add_test(tc, writes_and_parses_suus);
  tcase_add_test(tc, refuses_invalid_suus);
  tcase_add_test(tc, bimau_write_rejects_invalid_units);
  tcase_add_test(tc, bimau_parse_state_across_units);
  tcase_add_test(tc, bimau_parse_sweeps_and_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bimau_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
