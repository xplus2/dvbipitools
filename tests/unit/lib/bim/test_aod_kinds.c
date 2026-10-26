/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/aod.h"
#include "lib/bim/codec.h"
#include "../../fx_bim.h"

#define CS "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:"

static char *kinds_uris[4] = {CS "1", CS "2", CS "3", CS "4"};

static void put_signed(bitwriter_t *bw, int neg, uint64_t mag) { ck_assert_int_eq(bitwriter_put(bw, (uint64_t)neg, 1) || bitwriter_put_vluimsbf5(bw, mag), 0); }

static void raw_of(bim_di_instance_t *in, unsigned type, bitwriter_t *bw) {
  size_t n;
  const unsigned char *p = bitwriter_data(bw, &n);
  in->raw = malloc(n ? n : 1);
  ck_assert_ptr_nonnull(in->raw);
  memcpy(in->raw, p, n);
  in->nraw = n;
  in->type = type;
  bitwriter_free(bw);
}

static void inst_uq(bim_di_instance_t *in, unsigned type, int vmin_neg, uint64_t vmin, uint64_t vmax, int nbits) {
  bitwriter_t bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, type, 2), 0);
  put_signed(&bw, vmin_neg, vmin);
  put_signed(&bw, 0, vmax);
  ck_assert_int_eq(bitwriter_put(&bw, (uint64_t)nbits - 1, 5), 0);
  raw_of(in, type, &bw);
}

static void inst_nuq(bim_di_instance_t *in, unsigned type, unsigned nbits_data_minus1, unsigned nchar, int nbits, int short_matrix) {
  bitwriter_t bw;
  size_t n = (size_t)nchar << (nbits_data_minus1 + 1);
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, type, 2) || bitwriter_put_vluimsbf5(&bw, nbits_data_minus1) || bitwriter_put(&bw, nchar, 5), 0);
  put_signed(&bw, 0, 0);
  put_signed(&bw, 0, 100);
  ck_assert_int_eq(bitwriter_put(&bw, (uint64_t)nbits - 1, 5), 0);
  if (n > 4096) n = 4096;
  for (size_t i = 0; i < n - (short_matrix ? 1 : 0); i++) ck_assert_int_eq(bitwriter_put(&bw, i % ((1u << nbits) - 1), nbits), 0);
  raw_of(in, type, &bw);
}

static void inst_plain(bim_di_instance_t *in, unsigned type) {
  bitwriter_t bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, type, 2), 0);
  raw_of(in, type, &bw);
}

static void free_raws(bim_di_instance_t *in, size_t n) {
  for (size_t i = 0; i < n; i++) free(in[i].raw);
}

START_TEST(aod_build_reads_every_instance_kind) {
  bim_decoderinit_t di;
  bim_di_instance_t in[7];
  bim_di_mapping_t map[2];
  bim_di_typeref_t tr[4];
  uint64_t ids[2] = {0, 1};
  uint64_t ids2[1] = {2};
  bim_simple_t fl = {0};
  bim_simple_t st = {0};
  bim_type_t t0 = {0};
  bim_type_t t1 = {0};
  bim_type_t t2 = {0};
  const bim_type_t *types[3] = {&t0, &t1, &t2};
  bim_schema_t sc = {0};
  bim_aod_t *a = NULL;
  bim_err_t err;
  fl.codec = BIM_FLOAT;
  st.codec = BIM_UTF8;
  t0.simple = &fl;
  t1.simple = &st;
  t2.simple = &fl;
  sc.types = types;
  sc.ntypes = 3;
  memset(&di, 0, sizeof di);
  di.mpegb = 1;
  di.aod = 1;
  di.naod_types = 4;
  di.aod_types = kinds_uris;
  di.nschemas = 1;
  inst_plain(&in[0], 0);
  inst_uq(&in[1], 1, 1, 3, 9, 5);
  inst_nuq(&in[2], 2, 1, 2, 5, 0);
  inst_plain(&in[3], 3);
  di.instances = in;
  di.ninstances = 4;
  memset(map, 0, sizeof map);
  tr[0].schema = 0;
  tr[0].type = 0;
  tr[1].schema = 0;
  tr[1].type = 1;
  tr[2].schema = 9;
  tr[2].type = 0;
  tr[3].schema = 0;
  tr[3].type = 77;
  map[0].instances = ids;
  map[0].ninstances = 2;
  map[0].types = tr;
  map[0].ntypes = 4;
  map[1].instances = ids2;
  map[1].ninstances = 1;
  map[1].types = tr + 1;
  map[1].ntypes = 1;
  di.mappings = map;
  di.nmappings = 1;
  ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), 0);
  ck_assert_int_eq(a->nmaps, 2);
  ck_assert_ptr_nonnull(bim_aod_find(a, &fl));
  ck_assert_ptr_null(bim_aod_find(a, &(bim_simple_t){0}));
  bim_aod_free(a);
  bim_aod_free(NULL);

  di.nmappings = 2;
  ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
  ck_assert_str_eq(err.code, "LE400307");
  di.nmappings = 1;

  for (long n = 0;; n++) {
    int hit;
    int rc;
    a = NULL;
    alloc_fail_at(n);
    rc = bim_aod_build(&di, &di, &sc, 1, &a, &err);
    hit = alloc_fail_end();
    if (!rc) bim_aod_free(a);
    if (!hit) break;
  }
  free_raws(in, 4);
}
END_TEST

START_TEST(aod_build_rejects_invalid_instances) {
  bim_decoderinit_t di;
  bim_di_instance_t in[1];
  bim_aod_t *a = NULL;
  bim_err_t err;
  bim_schema_t sc = {0};
  char *bad_uris[4] = {"urn:unknown", CS "5", CS "12", CS ""};
  memset(&di, 0, sizeof di);
  di.mpegb = 1;
  di.aod = 1;
  di.naod_types = 4;
  di.nschemas = 1;
  di.instances = in;
  di.ninstances = 1;
  di.aod_types = bad_uris;
  for (unsigned t = 0; t < 4; t++) {
    inst_plain(&in[0], t);
    ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
    free(in[0].raw);
  }
  di.aod_types = kinds_uris;
  inst_plain(&in[0], 0);
  in[0].type = 9;
  ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
  free(in[0].raw);
  inst_nuq(&in[0], 2, 20, 1, 1, 0);
  ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
  free(in[0].raw);
  inst_nuq(&in[0], 2, 1, 2, 5, 1);
  ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
  {
    unsigned char *raw = in[0].raw;
    size_t full = in[0].nraw;
    for (size_t cut = 0; cut < full; cut++) {
      in[0].nraw = cut;
      (void)bim_aod_build(&di, &di, &sc, 1, &a, &err);
    }
    in[0].nraw = full;
    for (unsigned bit = 0; bit < full * 8; bit++) {
      raw[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      if (!bim_aod_build(&di, &di, &sc, 1, &a, &err)) bim_aod_free(a);
      raw[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
  }
  free(in[0].raw);
  {
    bitwriter_t bw;
    bitwriter_init(&bw);
    ck_assert_int_eq(bitwriter_put(&bw, 2, 2) || bitwriter_put_vluimsbf5(&bw, 40), 0);
    raw_of(&in[0], 2, &bw);
    ck_assert_int_eq(bim_aod_build(&di, &di, &sc, 1, &a, &err), -1);
    free(in[0].raw);
  }
}
END_TEST

static bim_aod_t *manual_aod(const bim_simple_t *s, int preserve, int kind, int use_nuq) {
  bim_aod_t *a = calloc(1, sizeof *a);
  int *ids = calloc(1, sizeof *ids);
  ck_assert_ptr_nonnull(a);
  ck_assert_ptr_nonnull(ids);
  a->insts = calloc(1, sizeof *a->insts);
  a->maps = calloc(1, sizeof *a->maps);
  ck_assert_ptr_nonnull(a->insts);
  ck_assert_ptr_nonnull(a->maps);
  a->ninsts = 1;
  a->nmaps = 1;
  a->insts[0].kind = (bim_aod_kind_t)kind;
  a->insts[0].vmin = 0;
  a->insts[0].vmax = 30;
  a->insts[0].nbits = 5;
  if (use_nuq) {
    a->insts[0].nbits_data = 2;
    a->insts[0].nchar = 2;
    a->insts[0].matrix = calloc(8, sizeof(uint32_t));
    ck_assert_ptr_nonnull(a->insts[0].matrix);
    for (int i = 0; i < 8; i++) a->insts[0].matrix[i] = (uint32_t)(i * 4);
  }
  a->maps[0].simple = s;
  a->maps[0].preserve = preserve;
  a->maps[0].inst = a->maps[0].heap_inst = ids;
  a->maps[0].ninst = 1;
  return a;
}

static int enc_one(const bim_simple_t *s, const char *v, bim_aod_t *a, const bim_encode_opts_t *base, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_encode_opts_t eo = *base;
  eo.aod = a;
  return bim_encode_simple(s, v, &eo, bw, sw);
}

START_TEST(aod_values_round_trip_through_every_decoder_kind) {
  bim_simple_t fl = {0};
  bim_simple_t str = {0};
  static const char *const prefixes[2] = {"p", ""};
  bim_schema_t schemas[2];
  fl.codec = BIM_FLOAT;
  str.codec = BIM_UTF8;
  memset(schemas, 0, sizeof schemas);
  schemas[0].known = schemas[1].known = 1;
  for (int variant = 0; variant < 8; variant++) {
    int kind = variant < 2 ? BIM_AOD_UQ : variant < 4 ? BIM_AOD_NUQ : variant < 6 ? BIM_AOD_QNAME : BIM_AOD_ZLIB;
    int preserve = variant & 1;
    const bim_simple_t *s = kind == BIM_AOD_QNAME || kind == BIM_AOD_ZLIB ? &str : &fl;
    static const char *const vals[] = {"0", "1.5", "30", "31", "-1", "NaN", "abc", "p:name q", "p:", "9:x", "1:y 0:z", "x:y"};
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
      bim_aod_t *ea = manual_aod(s, preserve, kind, kind == BIM_AOD_NUQ);
      bim_aod_t *da = manual_aod(s, preserve, kind, kind == BIM_AOD_NUQ);
      bim_encode_opts_t eo;
      bim_decode_opts_t dopt;
      bitwriter_t bw;
      strrepo_writer_t sw;
      bitreader_t br;
      const unsigned char *p;
      unsigned char *copy;
      size_t n;
      char *out = NULL;
      memset(&eo, 0, sizeof eo);
      eo.mpegb = 1;
      eo.lax_schema = -1;
      eo.schemas = schemas;
      eo.nschemas = 2;
      eo.schema_prefix = prefixes;
      eo.deflate = ident;
      eo.aod_plain = variant == 1;
      bitwriter_init(&bw);
      ck_assert_int_eq(strrepo_writer_init(&sw), 0);
      for (int rep = 0; rep < 3; rep++) {
        int rc = enc_one(s, vals[i], ea, &eo, &bw, &sw);
        if (rc) break;
      }
      p = bitwriter_data(&bw, &n);
      copy = malloc(n ? n : 1);
      ck_assert_ptr_nonnull(copy);
      if (n) memcpy(copy, p, n);
      memset(&dopt, 0, sizeof dopt);
      dopt.mpegb = 1;
      dopt.lax_schema = -1;
      dopt.schemas = schemas;
      dopt.nschemas = 2;
      dopt.schema_prefix = prefixes;
      dopt.inflate = ident;
      dopt.aod = da;
      bitreader_init(&br, copy, n);
      for (int rep = 0; rep < 3; rep++) {
        if (bim_decode_simple(s, &dopt, &br, NULL, &out)) break;
        free(out);
        out = NULL;
      }
      for (size_t cut = 0; cut < n; cut++) {
        bitreader_init(&br, copy, cut);
        if (!bim_decode_simple(s, &dopt, &br, NULL, &out)) free(out);
      }
      for (size_t bit = 0; bit < n * 8; bit++) {
        copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
        bitreader_init(&br, copy, n);
        if (!bim_decode_simple(s, &dopt, &br, NULL, &out)) free(out);
        copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      }
      dopt.schema_prefix = NULL;
      dopt.inflate = NULL;
      bitreader_init(&br, copy, n);
      if (!bim_decode_simple(s, &dopt, &br, NULL, &out)) free(out);
      free(copy);
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      bim_aod_free(ea);
      bim_aod_free(da);
    }
  }
}
END_TEST

static Suite *aod_suite(void) {
  Suite *s = suite_create("bim_aod_kinds");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aod_build_reads_every_instance_kind);
  tcase_add_test(tc, aod_build_rejects_invalid_instances);
  tcase_add_test(tc, aod_values_round_trip_through_every_decoder_kind);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aod_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
