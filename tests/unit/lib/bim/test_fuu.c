/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/fuu.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"

static const char schema_t[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:complexType name=\"ItemType\"><xs:sequence><xs:element name=\"Label\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"n\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"RootType\"><xs:sequence>"
  "<xs:element name=\"Item\" type=\"ItemType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"id\" type=\"xs:string\"/></xs:complexType>"
  "<xs:element name=\"Root\" type=\"RootType\"/>"
  "<xs:element name=\"Speed\" type=\"xs:float\"/>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  bim_decoderinit_t di;
  bim_fuu_state_t st;
  strrepo_writer_t sw;
} fx_t;

static void fx_init(fx_t *f) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xml_doc_t *doc = NULL;
  memset(f, 0, sizeof *f);
  f->set = xsd_set_new();
  ck_assert_ptr_nonnull(f->set);
  ck_assert_int_eq(xml_doc_parse(schema_t, strlen(schema_t), &doc, NULL), 0);
  if (xsd_set_add_doc(f->set, doc, "t.xsd") || xsd_set_resolve(f->set) || xsd_compile(f->set, &o, &f->c)) ck_abort_msg("%s %s", xsd_set_error_code(f->set), xsd_set_error(f->set));
  f->di.mpegb = 1;
  f->di.nschemas = 1;
  f->st.di = &f->di;
  f->st.schemas = &f->c->schema;
  f->st.nschemas = 1;
  f->st.opts.lax_schema = -1;
  ck_assert_int_eq(strrepo_writer_init(&f->sw), 0);
}

static void fx_free(fx_t *f) {
  bim_fuu_state_close(&f->st);
  strrepo_writer_free(&f->sw);
  xsd_set_free(f->set);
}

static bim_node_t *item(const char *n, const char *label) {
  bim_node_t *i = bim_node_new("Item");
  ck_assert_ptr_nonnull(i);
  if (n) ck_assert_int_eq(bim_node_set_attr(i, "n", n), 0);
  if (label) ck_assert_ptr_nonnull(bim_node_add_new(i, "Label", label));
  return i;
}

static void put_payload(fx_t *f, bitwriter_t *bw, const char *type, const bim_node_t *n) {
  strrepo_writer_t sw;
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload(xsd_compiled_type(f->c, NULL, type), n, 1, bw, &sw), 0);
  strrepo_writer_free(&sw);
}

static void put_text(bitwriter_t *bw, const char *s) {
  ck_assert_int_eq(bitwriter_put_vluimsbf5(bw, strlen(s)) || bitwriter_put_bytes(bw, (const unsigned char *)s, strlen(s)), 0);
}

static int run(fx_t *f, bitwriter_t *bw, bim_err_t *err) {
  size_t n;
  bim_bits_t b;
  bim_fuu_t u;
  int rc;
  b.bits = (unsigned char *)bitwriter_data(bw, &n);
  b.nbits = (uint64_t)n * 8;
  if (bim_fuu_parse(&f->st, &b, &u, err)) return -1;
  rc = bim_fuu_apply(&f->st, &u, err);
  bim_fuu_free(&u);
  return rc;
}

static void ok(fx_t *f, bitwriter_t *bw) {
  bim_err_t err;
  if (run(f, bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  bitwriter_free(bw);
}

/* ContextModeCode 001, Root selector 00, ContextTBC end 11, OperandTBC */
static void head(bitwriter_t *bw, unsigned cmd, unsigned operand) {
  bitwriter_init(bw);
  ck_assert_int_eq(bitwriter_put(bw, cmd, 4) || bitwriter_put(bw, 1, 3) || bitwriter_put(bw, 0, 2) || bitwriter_put(bw, 3, 2) || bitwriter_put(bw, operand, 2), 0);
}

static void add_root(fx_t *f) {
  bitwriter_t bw;
  bim_node_t *r = bim_node_new("Root");
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, BIM_CMD_ADD, 4) || bitwriter_put(&bw, 1, 3) || bitwriter_put(&bw, 3, 2) || bitwriter_put(&bw, 0, 1), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "id", "r1"), 0);
  put_payload(f, &bw, "RootType", r);
  bim_node_free(r);
  ok(f, &bw);
}

/* ISO 23001-1 6.4, 6.5: absolute path, AddContent, ReplaceContent, DeleteContent, reset */
START_TEST(fuu_commands_edit_document) {
  fx_t f;
  bitwriter_t bw;
  bim_node_t *n;
  fx_init(&f);
  add_root(&f);
  ck_assert_str_eq(f.st.doc->name, "Root");
  ck_assert_str_eq(bim_node_attr(f.st.doc, "id"), "r1");

  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 0), 0);
  n = item("a", "x");
  put_payload(&f, &bw, "ItemType", n);
  bim_node_free(n);
  ok(&f, &bw);
  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 1), 0);
  n = item("b", NULL);
  put_payload(&f, &bw, "ItemType", n);
  bim_node_free(n);
  ok(&f, &bw);
  ck_assert_int_eq(f.st.doc->nkids, 2);
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[0], "n"), "a");
  ck_assert_str_eq(f.st.doc->kids[0]->kids[0]->text, "x");
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[1], "n"), "b");

  head(&bw, BIM_CMD_REPLACE, 2);
  put_text(&bw, "r2");
  ok(&f, &bw);
  ck_assert_str_eq(bim_node_attr(f.st.doc, "id"), "r2");

  head(&bw, BIM_CMD_REPLACE, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 0), 0);
  n = item("c", NULL);
  put_payload(&f, &bw, "ItemType", n);
  bim_node_free(n);
  ok(&f, &bw);
  ck_assert_int_eq(f.st.doc->nkids, 2);
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[0], "n"), "c");

  head(&bw, BIM_CMD_DELETE, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 1), 0);
  ok(&f, &bw);
  ck_assert_int_eq(f.st.doc->nkids, 1);

  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, BIM_CMD_RESET, 4), 0);
  ok(&f, &bw);
  ck_assert_ptr_null(f.st.doc);
  fx_free(&f);
}
END_TEST

/* 6.6.5.6: incremental position codes of ContextModeCode 011 */
START_TEST(fuu_multiple_payloads) {
  fx_t f;
  bitwriter_t bw;
  bim_node_t *n;
  fx_init(&f);
  add_root(&f);
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, BIM_CMD_ADD, 4) || bitwriter_put(&bw, 3, 3) || bitwriter_put(&bw, 0, 2) || bitwriter_put(&bw, 3, 2) || bitwriter_put(&bw, 1, 2) ||
                       bitwriter_put_vluimsbf5(&bw, 0) || bitwriter_put(&bw, 1, 2) || bitwriter_put(&bw, 3, 2),
                   0);
  n = item("p", NULL);
  put_payload(&f, &bw, "ItemType", n);
  bim_node_free(n);
  n = item("q", NULL);
  put_payload(&f, &bw, "ItemType", n);
  bim_node_free(n);
  ok(&f, &bw);
  ck_assert_int_eq(f.st.doc->nkids, 2);
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[0], "n"), "p");
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[1], "n"), "q");
  fx_free(&f);
}
END_TEST

static void expect(fx_t *f, bitwriter_t *bw, const char *code) {
  bim_err_t err;
  ck_assert_int_eq(run(f, bw, &err), -1);
  ck_assert_str_eq(err.code, code);
  bitwriter_free(bw);
}

/* LE400303 truncated, LE400304 range, LE400305 unsupported, LE400306 state */
START_TEST(fuu_rejects_invalid) {
  fx_t f;
  bitwriter_t bw;
  fx_init(&f);

  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 4), 0);
  expect(&f, &bw, "LE400304");

  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_flush(&bw), 0);
  expect(&f, &bw, "LE400303");

  head(&bw, BIM_CMD_DELETE, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 0), 0);
  expect(&f, &bw, "LE400306");

  add_root(&f);
  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 0), 0);
  {
    bim_node_t *n = item("a", NULL);
    put_payload(&f, &bw, "ItemType", n);
    bim_node_free(n);
  }
  ok(&f, &bw);
  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 0), 0);
  {
    bim_node_t *n = item("again", NULL);
    put_payload(&f, &bw, "ItemType", n);
    bim_node_free(n);
  }
  expect(&f, &bw, "LE400306");

  f.di.unit_size_code = 1;
  head(&bw, BIM_CMD_ADD, 1);
  ck_assert_int_eq(bitwriter_put_vluimsbf5(&bw, 3), 0);
  expect(&f, &bw, "LE400305");
  fx_free(&f);
}
END_TEST

static void uq_params(bim_di_instance_t *in, unsigned long vmax, int nbits) {
  bitwriter_t bw;
  size_t n;
  const unsigned char *p;
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 0, 1) || bitwriter_put_vluimsbf5(&bw, 0) || bitwriter_put(&bw, 0, 1) || bitwriter_put_vluimsbf5(&bw, vmax) || bitwriter_put(&bw, (uint64_t)nbits - 1, 5), 0);
  in->raw = malloc(8);
  ck_assert_ptr_nonnull(in->raw);
  p = bitwriter_data(&bw, &n);
  memcpy(in->raw, p, n);
  in->nraw = n;
  in->type = 0;
  bitwriter_free(&bw);
}

static const bim_simple_t *speed_simple(const fx_t *f) { return xsd_compiled_elem(f->c, NULL, "Speed")->type->simple; }

static uint64_t speed_code(const fx_t *f) {
  const bim_simple_t *s = speed_simple(f);
  for (int i = 1; i < f->c->schema.ntypes; i++)
    if (f->c->schema.types[i]->simple == s && !f->c->schema.types[i]->nsbc) return (uint64_t)i - 1;
  ck_abort_msg("float type not in schema");
  return 0;
}

static double decode_speed(fx_t *f, const unsigned char *bits, size_t n) {
  bitreader_t br;
  char *out = NULL;
  double v;
  bitreader_init(&br, bits, n);
  ck_assert_int_eq(bim_decode_simple(speed_simple(f), &f->st.opts, &br, NULL, &out), 0);
  v = strtod(out, NULL);
  free(out);
  return v;
}

/* ISO 23001-1 8.2, 8.5: UniformQuantizer mapped to float, with and without default decoder */
START_TEST(aod_uniform_quantizer_mapped_to_type) {
  fx_t f;
  bim_err_t err;
  bim_di_instance_t in;
  bim_di_mapping_t map;
  bim_di_typeref_t tr;
  uint64_t inst = 0;
  char *uri = "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:2";
  fx_init(&f);
  f.di.aod = 1;
  f.di.naod_types = 1;
  f.di.aod_types = &uri;
  uq_params(&in, 10, 4);
  f.di.instances = &in;
  f.di.ninstances = 1;
  tr.schema = 0;
  tr.type = speed_code(&f);
  memset(&map, 0, sizeof map);
  map.instances = &inst;
  map.ninstances = 1;
  map.types = &tr;
  map.ntypes = 1;
  f.di.mappings = &map;
  f.di.nmappings = 1;
  ck_assert_int_eq(bim_fuu_state_aod_init(&f.st, &err), 0);
  ck_assert_ptr_nonnull(f.st.opts.aod);
  ck_assert_int_eq(f.st.opts.aod->nmaps, 1);
  ck_assert_double_eq_tol(decode_speed(&f, (const unsigned char[]){0x30}, 1), 2.0, 1e-6);
  ck_assert_double_eq_tol(decode_speed(&f, (const unsigned char[]){0xF0}, 1), 10.0, 1e-6);
  bim_fuu_state_close(&f.st);

  map.preserve_default = 1;
  ck_assert_int_eq(bim_fuu_state_aod_init(&f.st, &err), 0);
  ck_assert_double_eq_tol(decode_speed(&f, (const unsigned char[]){0x98}, 1), 2.0, 1e-6);
  bim_fuu_state_close(&f.st);
  free(in.raw);
  fx_free(&f);
}
END_TEST

START_TEST(aod_rejects_invalid) {
  fx_t f;
  bim_err_t err;
  bim_aod_t *a = NULL;
  bim_di_instance_t in;
  bim_di_mapping_t map;
  bim_di_typeref_t tr[2];
  uint64_t inst = 0;
  char *bad = "urn:example:unknown";
  char *uri = "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:2";
  fx_init(&f);
  f.di.aod = 1;
  f.di.naod_types = 1;
  f.di.aod_types = &bad;
  uq_params(&in, 10, 4);
  f.di.instances = &in;
  f.di.ninstances = 1;
  ck_assert_int_eq(bim_aod_build(&f.di, &f.di, &f.c->schema, 1, &a, &err), -1);
  ck_assert_str_eq(err.code, "LE400307");

  f.di.aod_types = &uri;
  tr[0].schema = tr[1].schema = 0;
  tr[0].type = tr[1].type = speed_code(&f);
  memset(&map, 0, sizeof map);
  map.instances = &inst;
  map.ninstances = 1;
  map.types = tr;
  map.ntypes = 2;
  f.di.mappings = &map;
  f.di.nmappings = 1;
  ck_assert_int_eq(bim_aod_build(&f.di, &f.di, &f.c->schema, 1, &a, &err), -1);
  ck_assert_str_eq(err.code, "LE400307");
  free(in.raw);
  fx_free(&f);
}
END_TEST

/* ISO 23001-1 6.4.2, 8.3: new AdvancedOptimisedDecodersConfig in an FUU */
START_TEST(fuu_writer_reparameterizes) {
  fx_t f;
  bim_di_instance_t in;
  bim_di_mapping_t map;
  bim_di_typeref_t tr;
  bim_decoderinit_t cfg;
  bim_fuu_t u;
  bim_fuu_t g;
  bim_err_t err;
  bitwriter_t bw;
  bim_bits_t b;
  size_t n;
  uint64_t inst = 0;
  char *uri = "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:2";
  fx_init(&f);
  f.di.aod = 1;
  f.di.naod_types = 1;
  f.di.aod_types = &uri;
  uq_params(&in, 10, 4);
  tr.schema = 0;
  tr.type = speed_code(&f);
  memset(&map, 0, sizeof map);
  map.instances = &inst;
  map.ninstances = 1;
  map.types = &tr;
  map.ntypes = 1;
  memset(&cfg, 0, sizeof cfg);
  cfg.instances = &in;
  cfg.ninstances = 1;
  cfg.mappings = &map;
  cfg.nmappings = 1;
  memset(&u, 0, sizeof u);
  u.command = BIM_CMD_RESET;
  u.config = &cfg;
  bitwriter_init(&bw);
  if (bim_fuu_write(&f.st, &u, NULL, &f.sw, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  b.bits = (unsigned char *)bitwriter_data(&bw, &n);
  b.nbits = (uint64_t)n * 8;
  if (bim_fuu_parse(&f.st, &b, &g, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_int_eq(g.reparam, 0);
  ck_assert_ptr_nonnull(g.config);
  ck_assert_uint_eq(g.config->ninstances, 1);
  ck_assert_uint_eq(g.config->mappings[0].types[0].type, tr.type);
  ck_assert_int_eq(f.st.opts.aod->nmaps, 1);
  ck_assert_double_eq_tol(decode_speed(&f, (const unsigned char[]){0x30}, 1), 2.0, 1e-6);
  bim_fuu_free(&g);
  bitwriter_free(&bw);
  free(in.raw);
  fx_free(&f);
}
END_TEST

static Suite *fuu_suite(void) {
  Suite *s = suite_create("bim_fuu");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fuu_commands_edit_document);
  tcase_add_test(tc, fuu_multiple_payloads);
  tcase_add_test(tc, fuu_rejects_invalid);
  tcase_add_test(tc, aod_uniform_quantizer_mapped_to_type);
  tcase_add_test(tc, aod_rejects_invalid);
  tcase_add_test(tc, fuu_writer_reparameterizes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fuu_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
