/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/decoderinit.h"
#include "../../cfg_fixture.h"

#define LIT(s) (const unsigned char *)(s), sizeof(s) - 1

static const char di_mixed[] = "\x00\x10\x03"
                               "\x0b"
                               "ic2:siemens"
                               "\x1f"
                               "examples/mixed/mixedcontent.xsd"
                               "\x00"
                               "\x24"
                               "http://www.w3.org/XML/1998/namespace"
                               "\x00\x00"
                               "\x20"
                               "http://www.w3.org/2001/XMLSchema"
                               "\x00\x00\x00";

static const char di_fragref[] = "\x00\x0f\x01\x08\x03"
                                 "\x1a"
                                 "urn:mpeg:mpeg7:schema:2001"
                                 "\x1a"
                                 "examples/is/Mpeg7-2001.xsd"
                                 "\x02"
                                 "\x30"
                                 "urn:mpeg:mpeg7:cs:VisualDescriptorCodecCS:2001:4"
                                 "\x06\x00\x01\x02\x03\x04\x05"
                                 "\x30"
                                 "urn:mpeg:mpeg7:cs:VisualDescriptorCodecCS:2001:1"
                                 "\x00"
                                 "\x24"
                                 "http://www.w3.org/XML/1998/namespace"
                                 "\x00\x00"
                                 "\x20"
                                 "http://www.w3.org/2001/XMLSchema"
                                 "\x00\x00"
                                 "\x01\x01\x00";

START_TEST(parses_15938_and_23001_conformance) {
  bim_decoderinit_t di;
  bim_err_t err;
  size_t used;
  ck_assert_int_eq(bim_decoderinit_parse(LIT(di_mixed), 0, NULL, NULL, &di, &used, &err), 0);
  ck_assert_uint_eq(used, sizeof di_mixed - 1);
  ck_assert_uint_eq(di.reserved, 0x10);
  ck_assert_uint_eq(di.nschemas, 3);
  ck_assert_str_eq(di.schemas[0].uri, "ic2:siemens");
  ck_assert_str_eq(di.schemas[0].hint, "examples/mixed/mixedcontent.xsd");
  ck_assert_uint_eq(di.ninitial, 0);
  bim_decoderinit_free(&di);

  ck_assert_int_eq(bim_decoderinit_parse(LIT(di_mixed), 1, NULL, NULL, &di, &used, &err), 0);
  ck_assert_int_eq(di.no_advanced, 1);
  ck_assert_uint_eq(di.reserved, 0);
  bim_decoderinit_free(&di);

  ck_assert_int_eq(bim_decoderinit_parse(LIT(di_fragref), 1, NULL, NULL, &di, &used, &err), 0);
  ck_assert_uint_eq(used, sizeof di_fragref - 1);
  ck_assert_int_eq(di.no_advanced, 0);
  ck_assert_int_eq(di.frag_ref, 1);
  ck_assert_int_eq(di.prefix_table, 0);
  ck_assert_uint_eq(di.schemas[0].ncodecs, 2);
  ck_assert_uint_eq(di.schemas[0].codecs[0].ntypes, 6);
  ck_assert_uint_eq(di.schemas[0].codecs[0].types[5], 5);
  ck_assert_uint_eq(di.nfrag_formats, 1);
  ck_assert_uint_eq(di.frag_formats[0], 1);
  bim_decoderinit_free(&di);
}
END_TEST

static int counts(void *ctx, const char *uri, uint64_t *nt, uint64_t *ne) {
  (void)ctx;
  if (uri && strcmp(uri, "urn:add")) return -1;
  *nt = uri ? 5 : 40;
  *ne = uri ? 3 : 9;
  return 0;
}

static void build_full(bim_decoderinit_t *di) {
  static const unsigned char path[] = {0xA5, 0x80};
  static const unsigned char init[] = {0x01, 0x02, 0x03};
  memset(di, 0, sizeof *di);
  di->mpegb = 1;
  di->profile = 300;
  di->unit_size_code = 2;
  di->reserved = 0xF;
  di->insert = di->aod = di->add_schema = di->frag_ref = di->mpc_only = di->hier = di->cpt = di->prefix_table = 1;
  di->nschemas = 2;
  di->schemas = calloc(2, sizeof *di->schemas);
  di->schemas[0].uri = dup_checked("urn:a");
  di->schemas[0].hint = dup_checked("a.xsd");
  di->schemas[0].ncodecs = 1;
  di->schemas[0].codecs = calloc(1, sizeof *di->schemas[0].codecs);
  di->schemas[0].codecs[0].uri = dup_checked("urn:codec");
  di->schemas[0].codecs[0].ntypes = 2;
  di->schemas[0].codecs[0].types = calloc(2, sizeof(uint64_t));
  di->schemas[0].codecs[0].types[0] = 7;
  di->schemas[0].codecs[0].types[1] = 200;
  di->schemas[1].uri = dup_checked("urn:b");
  di->schemas[1].hint = dup_checked("");
  di->cpt_code_len = 12;
  di->ncpaths = 2;
  di->cpaths = calloc(2, sizeof *di->cpaths);
  for (int i = 0; i < 2; i++) {
    di->cpaths[i].nbits = 9;
    di->cpaths[i].bits = malloc(2);
    memcpy(di->cpaths[i].bits, path, 2);
    di->cpaths[i].code = (uint64_t)(0x101 + i);
  }
  di->naod_types = 3;
  di->aod_types = calloc(3, sizeof *di->aod_types);
  di->aod_types[0] = dup_checked("urn:mpeg:mpeg7:systems:SystemsAdvancedOptimisedDecodersCS:2004:1");
  di->aod_types[1] = dup_checked("urn:mpeg:mpeg7:systems:SystemsAdvancedOptimisedDecodersCS:2004:2");
  di->aod_types[2] = dup_checked("urn:mpeg:mpeg7:systems:SystemsAdvancedOptimisedDecodersCS:2004:3");
  di->ninstances = 2;
  di->instances = calloc(2, sizeof *di->instances);
  di->instances[0].type = 0;
  di->instances[0].nraw = 1;
  di->instances[0].raw = calloc(1, 1);
  di->instances[1].type = 2;
  di->instances[1].nraw = 2;
  di->instances[1].raw = malloc(2);
  di->instances[1].raw[0] = 0x80;
  di->instances[1].raw[1] = 0x55;
  di->nmappings = 1;
  di->mappings = calloc(1, sizeof *di->mappings);
  di->mappings[0].preserve_default = 1;
  di->mappings[0].ninstances = 2;
  di->mappings[0].instances = calloc(2, sizeof(uint64_t));
  di->mappings[0].instances[1] = 1;
  di->mappings[0].ntypes = 2;
  di->mappings[0].types = calloc(2, sizeof *di->mappings[0].types);
  di->mappings[0].types[0].schema = 1;
  di->mappings[0].types[0].type = 4;
  di->mappings[0].types[1].schema = 5;
  di->mappings[0].types[1].type = 1;
  di->nfrag_formats = 2;
  di->frag_formats = malloc(2);
  di->frag_formats[0] = 1;
  di->frag_formats[1] = 230;
  di->nadditional = 6;
  di->nknown = 1;
  di->known = calloc(1, sizeof *di->known);
  di->known[0].id = 3;
  di->known[0].uri = dup_checked("urn:add");
  di->known[0].hint = dup_checked("add.bim");
  di->known[0].castable.present = 1;
  di->known[0].castable.ncodes = 2;
  di->known[0].castable.codes = calloc(2, sizeof(uint64_t));
  di->known[0].castable.codes[0] = 1;
  di->known[0].castable.codes[1] = 4;
  di->known[0].substitutable.present = 1;
  di->known[0].substitutable.all = 1;
  di->schema_encoding = 1;
  di->castable.present = 1;
  di->castable.ncodes = 1;
  di->castable.codes = calloc(1, sizeof(uint64_t));
  di->castable.codes[0] = 39;
  di->nprefixes = 2;
  di->prefixes = calloc(2, sizeof *di->prefixes);
  di->prefixes[0].uri = dup_checked("urn:a");
  di->prefixes[0].prefix = dup_checked("a");
  di->prefixes[1].uri = dup_checked("urn:b");
  di->prefixes[1].prefix = dup_checked("b");
  di->ninitial = sizeof init;
  di->initial = malloc(sizeof init);
  memcpy(di->initial, init, sizeof init);
}

START_TEST(writes_and_parses_all_features) {
  bim_decoderinit_t in;
  bim_decoderinit_t out;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  size_t used;
  build_full(&in);
  bitwriter_init(&bw);
  if (bim_decoderinit_write(&in, counts, NULL, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  if (bim_decoderinit_parse(p, n, 1, counts, NULL, &out, &used, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_uint_eq(used, n);
  ck_assert_uint_eq(out.profile, 300);
  ck_assert_uint_eq(out.unit_size_code, 2);
  ck_assert_int_eq(out.insert && out.aod && out.add_schema && out.frag_ref && out.mpc_only && out.hier && out.cpt && out.prefix_table, 1);
  ck_assert_int_eq(out.add_schema_only, 0);
  ck_assert_str_eq(out.prefixes[1].prefix, "b");
  ck_assert_uint_eq(out.schemas[0].codecs[0].types[1], 200);
  ck_assert_uint_eq(out.ncpaths, 2);
  ck_assert_uint_eq(out.cpaths[1].nbits, 9);
  ck_assert_uint_eq(out.cpaths[1].bits[0], 0xA5);
  ck_assert_uint_eq(out.cpaths[1].bits[1], 0x80);
  ck_assert_uint_eq(out.cpaths[1].code, 0x102);
  ck_assert_uint_eq(out.instances[1].type, 2);
  ck_assert_uint_eq(out.instances[1].raw[1], 0x55);
  ck_assert_uint_eq(out.mappings[0].types[1].schema, 5);
  ck_assert_uint_eq(out.mappings[0].types[1].type, 1);
  ck_assert_uint_eq(out.frag_formats[1], 230);
  ck_assert_uint_eq(out.nadditional, 6);
  ck_assert_uint_eq(out.known[0].id, 3);
  ck_assert_uint_eq(out.known[0].castable.codes[1], 4);
  ck_assert_int_eq(out.known[0].substitutable.all, 1);
  ck_assert_uint_eq(out.castable.codes[0], 39);
  ck_assert_int_eq(out.substitutable.present, 0);
  ck_assert_uint_eq(out.ninitial, 3);
  ck_assert_uint_eq(out.initial[2], 3);
  bim_decoderinit_free(&out);
  bitwriter_free(&bw);
  bim_decoderinit_free(&in);
}
END_TEST

static const char *parse_code(const unsigned char *p, size_t n, int mpegb, bim_di_counts_fn cf) {
  static bim_err_t err;
  bim_decoderinit_t di;
  size_t used;
  if (!bim_decoderinit_parse(p, n, mpegb, cf, NULL, &di, &used, &err)) {
    bim_decoderinit_free(&di);
    return "ok";
  }
  return err.code;
}

START_TEST(refuses_invalid) {
  bim_decoderinit_t in;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  unsigned char buf[16];
  ck_assert_str_eq(parse_code(LIT("\x00\x10\x00\x00"), 0, NULL), "LE400292");
  ck_assert_str_eq(parse_code(LIT("\x00\x10\x01\x00\x00\x00\x00"), 0, NULL), "LE400292");
  ck_assert_str_eq(parse_code(LIT("\x00\x10\x01\x01"), 0, NULL), "LE400291");
  ck_assert_str_eq(parse_code(LIT("\x00\x0f\x02\x00\x01\x01\x01\x61\x00\x00\x00"), 1, NULL), "LE400293");
  ck_assert_str_eq(parse_code(LIT("\x00\x0f\x01\x10"), 1, NULL), "LE400294");
  ck_assert_str_eq(parse_code(LIT("\x00\x0f\x01\x20"), 1, NULL), "LE400294");

  build_full(&in);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&in, NULL, NULL, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400296");
  bitwriter_reset(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&in, counts, NULL, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_str_eq(parse_code(p, n, 1, NULL), "LE400296");
  ck_assert_str_eq(parse_code(p, n - 1, 1, counts), "LE400291");
  in.known[0].id = 2;
  bitwriter_reset(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&in, counts, NULL, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400298");
  bim_decoderinit_free(&in);
  bitwriter_free(&bw);

  memcpy(buf, "\x00\x0f\x01\x01\x01\x01\x61\x00\x00\x04\x08\x00\x80\x00\x00", 15);
  ck_assert_str_eq(parse_code(buf, 15, 1, NULL), "LE400297");
}
END_TEST

typedef void (*di_mut)(bim_decoderinit_t *di);

static void m_unit(bim_decoderinit_t *di) { di->unit_size_code = 9; }
static void m_reserved(bim_decoderinit_t *di) { di->reserved = 16; }
static void m_noadv(bim_decoderinit_t *di) { di->no_advanced = 1; }
static void m_notmpegb(bim_decoderinit_t *di) { di->mpegb = 0; }
static void m_only_without_add(bim_decoderinit_t *di) {
  di->add_schema = 0;
  di->add_schema_only = 1;
}
static void m_add_without_hier(bim_decoderinit_t *di) { di->hier = 0; }
static void m_zero_schemas(bim_decoderinit_t *di) { di->nschemas = 0; }
static void m_cpt_len(bim_decoderinit_t *di) { di->cpt_code_len = 65; }
static void m_cpt_code_zero(bim_decoderinit_t *di) { di->cpaths[0].code = 0; }
static void m_cpt_code_big(bim_decoderinit_t *di) { di->cpaths[1].code = 1ull << 20; }
static void m_inst_type(bim_decoderinit_t *di) { di->instances[0].type = 1; }
static void m_inst_range(bim_decoderinit_t *di) { di->instances[1].type = 9; }
static void m_map_reserved(bim_decoderinit_t *di) { di->mappings[0].reserved = 200; }
static void m_map_instance(bim_decoderinit_t *di) { di->mappings[0].instances[1] = 7; }
static void m_map_schema(bim_decoderinit_t *di) { di->mappings[0].types[0].schema = 99; }
static void m_frag_count(bim_decoderinit_t *di) { di->nfrag_formats = 300; }
static void m_known_low(bim_decoderinit_t *di) { di->known[0].id = di->nschemas; }
static void m_known_high(bim_decoderinit_t *di) { di->known[0].id = 7; }
static void m_encoding(bim_decoderinit_t *di) { di->schema_encoding = 300; }
static void m_counts_unknown(bim_decoderinit_t *di) {
  free(di->known[0].uri);
  di->known[0].uri = dup_checked("urn:zzz");
}

START_TEST(decoderinit_write_rejects_invalid_models) {
  static const di_mut muts[] = {m_unit,        m_reserved,    m_noadv,        m_notmpegb,   m_only_without_add, m_add_without_hier, m_zero_schemas, m_cpt_len,    m_cpt_code_zero,   m_cpt_code_big,
                                m_inst_type,   m_inst_range,  m_map_reserved, m_map_instance, m_map_schema,      m_frag_count,       m_known_low,    m_known_high, m_encoding,        m_counts_unknown};
  for (size_t k = 0; k < sizeof muts / sizeof *muts; k++) {
    bim_decoderinit_t di;
    bim_err_t err;
    bitwriter_t bw;
    size_t nschemas;
    size_t nfrag;
    build_full(&di);
    nschemas = di.nschemas;
    nfrag = di.nfrag_formats;
    muts[k](&di);
    bitwriter_init(&bw);
    ck_assert_msg(bim_decoderinit_write(&di, counts, NULL, &bw, &err) == -1, "mutation %zu accepted", k);
    ck_assert_ptr_nonnull(err.code);
    bitwriter_free(&bw);
    di.nschemas = nschemas;
    di.nfrag_formats = nfrag;
    bim_decoderinit_free(&di);
  }
}
END_TEST

START_TEST(decoderinit_aod_config_write_rejects_invalid) {
  bim_decoderinit_t di;
  bim_decoderinit_t cfg;
  bim_err_t err;
  bitwriter_t bw;
  build_full(&di);
  cfg = di;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_di_aod_config_write(&bw, &di, &cfg, &err), 0);
  bitwriter_free(&bw);
  di.instances[0].type = 1;
  cfg = di;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_di_aod_config_write(&bw, &di, &cfg, &err), -1);
  bitwriter_free(&bw);
  di.instances[0].type = 0;
  di.mappings[0].reserved = 200;
  cfg = di;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_di_aod_config_write(&bw, &di, &cfg, &err), -1);
  bitwriter_free(&bw);
  di.mappings[0].reserved = 0;
  bim_decoderinit_free(&di);
}
END_TEST

static void minimal(bim_decoderinit_t *di, int mpegb) {
  memset(di, 0, sizeof *di);
  di->mpegb = mpegb;
  di->profile = 5;
  di->nschemas = 1;
  di->schemas = calloc(1, sizeof *di->schemas);
  di->schemas[0].uri = dup_checked("urn:only");
  di->schemas[0].hint = dup_checked("");
}

START_TEST(decoderinit_writes_reduced_forms) {
  bim_decoderinit_t in;
  bim_decoderinit_t out;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  size_t used;
  size_t nknown = 0;
  for (int variant = 0; variant < 6; variant++) {
    if (variant == 0) minimal(&in, 0);
    else build_full(&in);
    switch (variant) {
      case 1: in.no_advanced = 0; in.cpt_complete = 1; break;
      case 2: in.prefix_table = 0; in.frag_ref = 0; in.aod = 0; in.cpt = 0; break;
      case 3:
        in.add_schema_only = 1;
        in.initial = in.initial;
        break;
      case 4:
        nknown = in.nknown;
        in.nknown = 0;
        break;
      case 5: in.insert = in.aod = in.add_schema = in.frag_ref = in.mpc_only = in.hier = in.cpt = in.prefix_table = 0; break;
      default: break;
    }
    bitwriter_init(&bw);
    if (bim_decoderinit_write(&in, counts, NULL, &bw, &err)) ck_abort_msg("variant %d %s %s", variant, err.code, err.msg);
    p = bitwriter_data(&bw, &n);
    if (bim_decoderinit_parse(p, n, in.mpegb, counts, NULL, &out, &used, &err)) ck_abort_msg("variant %d parse %s %s", variant, err.code, err.msg);
    bim_decoderinit_free(&out);
    bitwriter_free(&bw);
    if (variant == 4) in.nknown = nknown;
    bim_decoderinit_free(&in);
  }
}
END_TEST

START_TEST(decoderinit_write_and_parse_failures) {
  bim_decoderinit_t in;
  bim_decoderinit_t out;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  unsigned char *copy;
  size_t n;
  size_t used;
  build_full(&in);
  for (int pre = 0; pre < 90; pre++) {
    bitwriter_init(&bw);
    for (int k = 0; k < pre; k++) ck_assert_int_eq(bitwriter_put(&bw, 0x5A, 8), 0);
    alloc_fail_at(0);
    (void)bim_decoderinit_write(&in, counts, NULL, &bw, &err);
    alloc_fail_end();
    bitwriter_free(&bw);
  }
  for (long k = 0;; k++) {
    int hit;
    bitwriter_init(&bw);
    alloc_fail_at(k);
    (void)bim_decoderinit_write(&in, counts, NULL, &bw, &err);
    hit = alloc_fail_end();
    bitwriter_free(&bw);
    if (!hit) break;
  }
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&in, counts, NULL, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_decoderinit_parse(copy, n, 1, counts, NULL, &out, &used, &err);
    hit = alloc_fail_end();
    if (!rc) bim_decoderinit_free(&out);
    if (!hit) break;
  }
  for (size_t cut = 0; cut < n; cut++)
    if (!bim_decoderinit_parse(copy, cut, 1, counts, NULL, &out, &used, &err)) bim_decoderinit_free(&out);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    if (!bim_decoderinit_parse(copy, n, 1, counts, NULL, &out, &used, &err)) bim_decoderinit_free(&out);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    if (!bim_decoderinit_parse(copy, n, 1, NULL, NULL, &out, &used, &err)) bim_decoderinit_free(&out);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  free(copy);
  bitwriter_free(&bw);
  bim_decoderinit_free(&in);
}
END_TEST

static Suite *decoderinit_suite(void) {
  Suite *s = suite_create("bim_decoderinit");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parses_15938_and_23001_conformance);
  tcase_add_test(tc, writes_and_parses_all_features);
  tcase_add_test(tc, refuses_invalid);
  tcase_add_test(tc, decoderinit_write_rejects_invalid_models);
  tcase_add_test(tc, decoderinit_aod_config_write_rejects_invalid);
  tcase_add_test(tc, decoderinit_writes_reduced_forms);
  tcase_add_test(tc, decoderinit_write_and_parse_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(decoderinit_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
