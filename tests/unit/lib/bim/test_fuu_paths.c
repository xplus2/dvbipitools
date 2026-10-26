/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/fuu.h"
#include "lib/bim/fuu/priv.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"

static const char schema_p[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:complexType name=\"BaseT\"><xs:sequence><xs:element name=\"Name\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"id\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"DerivedT\"><xs:complexContent><xs:extension base=\"BaseT\"><xs:sequence>"
  "<xs:element name=\"Extra\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"EntryT\"><xs:sequence><xs:element name=\"Key\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"k\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"MixT\" mixed=\"true\"><xs:sequence>"
  "<xs:element name=\"Em\" type=\"EntryT\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ChoT\"><xs:choice maxOccurs=\"unbounded\">"
  "<xs:element name=\"A\" type=\"EntryT\"/><xs:element name=\"B\" type=\"EntryT\"/></xs:choice></xs:complexType>"
  "<xs:complexType name=\"TxtT\"><xs:simpleContent><xs:extension base=\"xs:string\">"
  "<xs:attribute name=\"u\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:complexType name=\"BoxT\"><xs:sequence>"
  "<xs:element name=\"Base\" type=\"BaseT\" minOccurs=\"0\"/>"
  "<xs:element ref=\"Head\" minOccurs=\"0\"/>"
  "<xs:element name=\"Mix\" type=\"MixT\" minOccurs=\"0\" maxOccurs=\"3\"/>"
  "<xs:element name=\"Many\" type=\"EntryT\" minOccurs=\"0\" maxOccurs=\"40\"/>"
  "<xs:element name=\"Few\" type=\"EntryT\" minOccurs=\"0\" maxOccurs=\"3\"/>"
  "<xs:element name=\"Cho\" type=\"ChoT\" minOccurs=\"0\"/>"
  "<xs:element name=\"Txt\" type=\"TxtT\" minOccurs=\"0\"/>"
  "<xs:element name=\"Val\" type=\"xs:string\" minOccurs=\"0\"/>"
  "<xs:any namespace=\"##other\" processContents=\"lax\" minOccurs=\"0\"/>"
  "</xs:sequence><xs:attribute name=\"a\" type=\"xs:string\"/><xs:anyAttribute namespace=\"##other\" processContents=\"lax\"/></xs:complexType>"
  "<xs:element name=\"Head\" type=\"BaseT\"/>"
  "<xs:element name=\"HeadAlt\" type=\"DerivedT\" substitutionGroup=\"Head\"/>"
  "<xs:element name=\"Box\" type=\"BoxT\"/>"
  "<xs:element name=\"Other\" type=\"EntryT\"/>"
  "<xs:complexType name=\"NodeT\"><xs:sequence><xs:element name=\"Kid\" type=\"NodeT\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Tree\" type=\"NodeT\"/>"
  "<xs:element name=\"Speed\" type=\"xs:float\"/>"
  "<xs:attribute name=\"gattr\" type=\"xs:string\"/>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  bim_decoderinit_t di;
  bim_fuu_state_t st;
  strrepo_writer_t sw;
} fx_t;

static void fx_init(fx_t *f, int insert, int mpc_only) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xml_doc_t *doc = NULL;
  memset(f, 0, sizeof *f);
  f->set = xsd_set_new();
  ck_assert_ptr_nonnull(f->set);
  ck_assert_int_eq(xml_doc_parse(schema_p, strlen(schema_p), &doc, NULL), 0);
  if (xsd_set_add_doc(f->set, doc, "p.xsd") || xsd_set_resolve(f->set) || xsd_compile(f->set, &o, &f->c)) ck_abort_msg("%s %s", xsd_set_error_code(f->set), xsd_set_error(f->set));
  f->di.mpegb = 1;
  f->di.nschemas = 1;
  f->di.insert = insert;
  f->di.mpc_only = mpc_only;
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

static const bim_type_t *ty(const fx_t *f, const char *name) {
  const bim_type_t *t = xsd_compiled_type(f->c, NULL, name);
  ck_assert_ptr_nonnull(t);
  return t;
}

static const bim_type_t *gty(const fx_t *f, const char *elem) {
  const bim_elem_t *e = xsd_compiled_elem(f->c, NULL, elem);
  ck_assert_ptr_nonnull(e);
  return e->type;
}

static bim_tbc_t tb(bim_tbc_kind_t kind, const char *name, const bim_type_t *type) {
  bim_tbc_t t;
  memset(&t, 0, sizeof t);
  t.kind = kind;
  t.name = name;
  t.type = type;
  return t;
}

static bim_tbc_t te(const char *name, const bim_type_t *type) { return tb(BIM_TBC_ELEM, name, type); }

static bim_tbc_t ta(const char *name, const bim_simple_t *s) {
  bim_tbc_t t = tb(BIM_TBC_ATTR, name, NULL);
  t.simple = s;
  return t;
}

typedef struct {
  bim_fuu_t u;
  bim_tbc_t tbcs[16];
  uint64_t pos[64];
  bim_node_t *payloads[8];
  char *values[8];
} job_t;

static void job_init(job_t *j, unsigned cmd, unsigned mode) {
  memset(j, 0, sizeof *j);
  j->u.command = cmd;
  j->u.mode = mode;
  j->u.tbcs = j->tbcs;
  j->u.pos = j->pos;
  j->u.payloads = j->payloads;
  j->u.values = j->values;
  j->u.npayloads = 1;
}

static void job_path(job_t *j, size_t n, const bim_tbc_t *t) {
  memcpy(j->tbcs, t, n * sizeof *t);
  j->u.ntbcs = n;
}

static void job_done(job_t *j) {
  for (size_t i = 0; i < j->u.npayloads && i < 8; i++) {
    bim_node_free(j->payloads[i]);
    j->payloads[i] = NULL;
  }
}

static int wr(fx_t *f, const bim_fuu_t *u, bitwriter_t *bw, bim_err_t *err) {
  bitwriter_init(bw);
  return bim_fuu_write(&f->st, u, NULL, &f->sw, bw, err);
}

static int parse_bw(fx_t *f, bitwriter_t *bw, bim_fuu_t *g, bim_err_t *err) {
  size_t n;
  bim_bits_t b = {NULL, 0, NULL};
  b.bits = bitwriter_data(bw, &n);
  b.nbits = (uint64_t)n * 8;
  return bim_fuu_parse(&f->st, &b, g, err);
}

static void rt(fx_t *f, job_t *j) {
  bim_err_t err;
  bitwriter_t bw;
  bim_fuu_t g;
  if (wr(f, &j->u, &bw, &err)) ck_abort_msg("write %s %s", err.code, err.msg);
  if (parse_bw(f, &bw, &g, &err)) ck_abort_msg("parse %s %s", err.code, err.msg);
  ck_assert_uint_eq(g.command, j->u.command);
  ck_assert_uint_eq(g.ntbcs, j->u.ntbcs);
  if (bim_fuu_apply(&f->st, &g, &err)) ck_abort_msg("apply %s %s", err.code, err.msg);
  bim_fuu_free(&g);
  bitwriter_free(&bw);
  job_done(j);
}

static void wr_fail_at(int line, fx_t *f, job_t *j, const char *code) {
  bim_err_t err;
  bitwriter_t bw;
  ck_assert_msg(wr(f, &j->u, &bw, &err) == -1, "line %d wrote", line);
  ck_assert_msg(!strcmp(err.code, code), "line %d code %s", line, err.code);
  bitwriter_free(&bw);
  job_done(j);
}

#define wr_fail(f, j, code) wr_fail_at(__LINE__, f, j, code)

static void apply_fail(fx_t *f, job_t *j, const char *code) {
  bim_err_t err;
  bitwriter_t bw;
  bim_fuu_t g;
  if (wr(f, &j->u, &bw, &err)) ck_abort_msg("write %s %s", err.code, err.msg);
  if (parse_bw(f, &bw, &g, &err)) ck_abort_msg("parse %s %s", err.code, err.msg);
  ck_assert_int_eq(bim_fuu_apply(&f->st, &g, &err), -1);
  ck_assert_str_eq(err.code, code);
  bim_fuu_free(&g);
  bitwriter_free(&bw);
  job_done(j);
}

static void add_box(fx_t *f) {
  job_t j;
  bim_tbc_t p[2];
  job_init(&j, BIM_CMD_ADD, 1);
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(f, "Box"));
  job_path(&j, 2, p);
  j.payloads[0] = bim_node_new("Box");
  ck_assert_int_eq(bim_node_set_attr(j.payloads[0], "a", "1"), 0);
  rt(f, &j);
  ck_assert_str_eq(f->st.doc->name, "Box");
}

static void add_child(fx_t *f, unsigned cmd, const char *name, const bim_type_t *type, uint64_t pos, bim_node_t *n) {
  job_t j;
  bim_tbc_t p[3];
  job_init(&j, cmd, 1);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, ty(f, "BoxT"));
  p[2] = te(name, type);
  job_path(&j, 3, p);
  j.pos[2] = pos;
  j.payloads[0] = n;
  rt(f, &j);
}

static bim_node_t *cho(void) {
  bim_node_t *c = bim_node_new("Cho");
  ck_assert_ptr_nonnull(c);
  ck_assert_ptr_nonnull(bim_node_add_new(c, "A", NULL));
  return c;
}

static bim_node_t *entry(const char *name, const char *key, const char *k) {
  bim_node_t *e = bim_node_new(name);
  ck_assert_ptr_nonnull(e);
  if (key) ck_assert_ptr_nonnull(bim_node_add_new(e, "Key", key));
  if (k) ck_assert_int_eq(bim_node_set_attr(e, "k", k), 0);
  return e;
}

START_TEST(fuu_position_code_kinds) {
  fx_t f;
  fx_init(&f, 0, 0);
  add_box(&f);
  add_child(&f, BIM_CMD_ADD, "Few", ty(&f, "EntryT"), 0, entry("Few", "a", NULL));
  add_child(&f, BIM_CMD_ADD, "Few", ty(&f, "EntryT"), 1, entry("Few", "b", NULL));
  add_child(&f, BIM_CMD_ADD, "Many", ty(&f, "EntryT"), 0, entry("Many", "a", NULL));
  add_child(&f, BIM_CMD_ADD, "Many", ty(&f, "EntryT"), 20, entry("Many", "b", NULL));
  add_child(&f, BIM_CMD_ADD, "Cho", ty(&f, "ChoT"), 0, cho());
  add_child(&f, BIM_CMD_ADD, "Txt", ty(&f, "TxtT"), 0, bim_node_new("Txt"));
  ck_assert_int_eq(f.st.doc->nkids, 6);
  fx_free(&f);
}
END_TEST

static const bim_type_t *kid_type(const bim_type_t *p, const char *name) {
  for (int i = 0; i < p->nsbc; i++)
    if (p->sbc[i].name && !strcmp(p->sbc[i].name, name)) return p->sbc[i].type;
  ck_abort_msg("no child %s", name);
  return NULL;
}

static const bim_simple_t *attr_simple(const bim_type_t *p, const char *name) {
  for (int i = 0; i < p->nsbc; i++)
    if (p->sbc[i].kind == BIM_SBC_ATTR && !strcmp(p->sbc[i].name, name)) return p->sbc[i].simple;
  ck_abort_msg("no attr %s", name);
  return NULL;
}

static uint64_t rat(uint64_t i) {
  int j = 1;
  while (j < 63 && (1ULL << j) <= i + 1) j++;
  return (2 * (i + 1) + 1 - (1ULL << j)) << (64 - j);
}

static void op_attr(fx_t *f, unsigned cmd, const bim_tbc_t *path, size_t n, const char *name, const bim_simple_t *s, char *value) {
  job_t j;
  bim_tbc_t p[8];
  memcpy(p, path, n * sizeof p[0]);
  p[n] = ta(name, s);
  job_init(&j, cmd, 1);
  job_path(&j, n + 1, p);
  j.values[0] = value;
  rt(f, &j);
}

START_TEST(fuu_attribute_and_simple_operands) {
  fx_t f;
  job_t j;
  bim_tbc_t base[3];
  bim_tbc_t txt[3];
  fx_init(&f, 0, 0);
  add_box(&f);
  base[0] = te("Box", gty(&f, "Box"));
  base[1] = te("Base", ty(&f, "BaseT"));
  base[2] = tb(BIM_TBC_END, NULL, NULL);
  add_child(&f, BIM_CMD_ADD, "Base", ty(&f, "BaseT"), 0, bim_node_new("Base"));
  op_attr(&f, BIM_CMD_ADD, base, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), "x1");
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[0], "id"), "x1");
  op_attr(&f, BIM_CMD_REPLACE, base, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), "x2");
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[0], "id"), "x2");
  op_attr(&f, BIM_CMD_DELETE, base, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), NULL);
  ck_assert_ptr_null(bim_node_attr(f.st.doc->kids[0], "id"));

  add_child(&f, BIM_CMD_ADD, "Txt", ty(&f, "TxtT"), 0, bim_node_new("Txt"));
  txt[0] = te("Box", gty(&f, "Box"));
  txt[1] = te("Txt", ty(&f, "TxtT"));
  txt[2] = tb(BIM_TBC_END, NULL, NULL);
  txt[1].has_pos = 0;
  for (int k = 0; k < 3; k++) {
    unsigned cmd[3] = {BIM_CMD_REPLACE, BIM_CMD_DELETE, BIM_CMD_ADD};
    bim_tbc_t p[4];
    memcpy(p, txt, sizeof txt);
    p[3] = tb(BIM_TBC_SIMPLE, NULL, NULL);
    p[3].simple = &bim_xs_string;
    job_init(&j, cmd[k], 1);
    job_path(&j, 4, p);
    j.values[0] = "text";
    rt(&f, &j);
  }
  op_attr(&f, BIM_CMD_ADD, txt, 3, "u", attr_simple(ty(&f, "TxtT"), "u"), "uu");
  fx_free(&f);
}
END_TEST

START_TEST(fuu_type_and_substitution_codes) {
  fx_t f;
  bim_node_t *n;
  bim_tbc_t p[4];
  bim_tbc_t q[3];
  job_t j;
  fx_init(&f, 0, 0);
  add_box(&f);
  n = bim_node_new("Base");
  ck_assert_ptr_nonnull(bim_node_add_new(n, "Name", "nm"));
  ck_assert_ptr_nonnull(bim_node_add_new(n, "Extra", "ex"));
  add_child(&f, BIM_CMD_ADD, "Base", ty(&f, "DerivedT"), 0, n);
  add_child(&f, BIM_CMD_ADD, "Head", ty(&f, "BaseT"), 0, bim_node_new("Head"));
  add_child(&f, BIM_CMD_DELETE, "Head", ty(&f, "BaseT"), 0, NULL);
  add_child(&f, BIM_CMD_ADD, "HeadAlt", ty(&f, "DerivedT"), 0, bim_node_new("HeadAlt"));
  ck_assert_int_eq(f.st.doc->nkids, 2);
  ck_assert_str_eq(f.st.doc->kids[1]->name, "HeadAlt");

  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("HeadAlt", ty(&f, "DerivedT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = ta("id", attr_simple(ty(&f, "BaseT"), "id"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 4, p);
  j.values[0] = "h";
  rt(&f, &j);
  ck_assert_str_eq(bim_node_attr(f.st.doc->kids[1], "id"), "h");

  q[0] = te("Box", gty(&f, "Box"));
  q[1] = tb(BIM_TBC_END, NULL, NULL);
  q[2] = te("Nope", ty(&f, "EntryT"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, q);
  j.payloads[0] = bim_node_new("Nope");
  wr_fail(&f, &j, "LE400304");
  fx_free(&f);
}
END_TEST

START_TEST(fuu_relative_paths) {
  fx_t f;
  job_t j;
  bim_tbc_t p[4];
  fx_init(&f, 0, 0);
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Few", ty(&f, "EntryT"));
  job_init(&j, BIM_CMD_ADD, 2);
  job_path(&j, 2, p);
  j.tbcs[0] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[1] = te("Few", ty(&f, "EntryT"));
  j.payloads[0] = entry("Few", "r", NULL);
  wr_fail(&f, &j, "LE400306");

  add_child(&f, BIM_CMD_ADD, "Few", ty(&f, "EntryT"), 0, entry("Few", "a", NULL));
  job_init(&j, BIM_CMD_ADD, 2);
  j.tbcs[0] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[1] = te("Few", ty(&f, "EntryT"));
  j.u.ntbcs = 2;
  j.pos[1] = 1;
  j.payloads[0] = entry("Few", "b", NULL);
  rt(&f, &j);
  ck_assert_int_eq(f.st.doc->nkids, 2);

  add_child(&f, BIM_CMD_ADD, "Base", ty(&f, "BaseT"), 0, bim_node_new("Base"));
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("Base", ty(&f, "BaseT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  op_attr(&f, BIM_CMD_ADD, p, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), "z");
  job_init(&j, BIM_CMD_ADD, 2);
  j.tbcs[0] = tb(BIM_TBC_PARENT, NULL, NULL);
  j.tbcs[1] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[2] = te("Few", ty(&f, "EntryT"));
  j.u.ntbcs = 3;
  j.pos[2] = 2;
  j.payloads[0] = entry("Few", "c", NULL);
  rt(&f, &j);
  ck_assert_int_eq(f.st.doc->nkids, 4);

  job_init(&j, BIM_CMD_ADD, 2);
  j.tbcs[0] = tb(BIM_TBC_PARENT, NULL, NULL);
  j.tbcs[1] = tb(BIM_TBC_PARENT, NULL, NULL);
  j.tbcs[2] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[3] = te("Few", ty(&f, "EntryT"));
  j.u.ntbcs = 4;
  j.payloads[0] = entry("Few", "d", NULL);
  wr_fail(&f, &j, "LE400304");
  fx_free(&f);
}
END_TEST

static void multi(fx_t *f, int insert) {
  job_t j;
  bim_tbc_t p[3];
  (void)insert;
  add_box(f);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Many", ty(f, "EntryT"));
  job_init(&j, BIM_CMD_ADD, 3);
  job_path(&j, 3, p);
  j.u.npayloads = 3;
  j.pos[2] = insert ? rat(0) : 0;
  j.pos[5] = insert ? rat(1) : 1;
  j.pos[8] = insert ? rat(4) : 5;
  j.payloads[0] = entry("Many", "m0", NULL);
  j.payloads[1] = entry("Many", "m1", NULL);
  j.payloads[2] = entry("Many", "m5", NULL);
  rt(f, &j);
  ck_assert_int_eq(f->st.doc->nkids, 3);
}

START_TEST(fuu_multi_payload_positions) {
  fx_t f;
  fx_init(&f, 0, 0);
  multi(&f, 0);
  fx_free(&f);
  fx_init(&f, 1, 0);
  multi(&f, 1);
  fx_free(&f);
  fx_init(&f, 0, 1);
  multi(&f, 0);
  fx_free(&f);
}
END_TEST

static void sweep(fx_t *f, job_t *j) {
  bim_err_t err;
  bitwriter_t bw;
  bim_fuu_t g;
  bim_bits_t b = {NULL, 0, NULL};
  size_t n;
  unsigned char *copy;
  if (wr(f, &j->u, &bw, &err)) ck_abort_msg("write %s %s", err.code, err.msg);
  b.bits = bitwriter_data(&bw, &n);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  for (uint64_t cut = 0; cut <= (uint64_t)n * 8; cut++) {
    b.bits = bitwriter_data(&bw, &n);
    b.nbits = cut;
    if (!bim_fuu_parse(&f->st, &b, &g, &err)) bim_fuu_free(&g);
    else ck_assert_msg(!strncmp(err.code, "LE4003", 6) || !strncmp(err.code, "LE4", 3), "code %s", err.code);
  }
  for (uint64_t bit = 0; bit < (uint64_t)n * 8; bit++) {
    memcpy(copy, bitwriter_data(&bw, &n), n);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    b.bits = copy;
    b.nbits = (uint64_t)n * 8;
    if (!bim_fuu_parse(&f->st, &b, &g, &err)) bim_fuu_free(&g);
  }
  free(copy);
  bitwriter_free(&bw);
}

START_TEST(fuu_parse_sweeps) {
  fx_t f;
  job_t j;
  bim_tbc_t p[4];
  fx_init(&f, 0, 0);
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("Mix", ty(&f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = tb(BIM_TBC_CHARDATA, NULL, NULL);
  p[3].simple = &bim_xs_string;
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 4, p);
  j.values[0] = "hello";
  sweep(&f, &j);

  p[1] = te("Base", ty(&f, "DerivedT"));
  p[3] = te("Extra", kid_type(ty(&f, "DerivedT"), "Extra"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 4, p);
  j.payloads[0] = bim_node_new("Extra");
  ck_assert_ptr_nonnull(bim_node_add_new(j.payloads[0], "Extra", "v"));
  sweep(&f, &j);
  job_done(&j);
  fx_free(&f);
}
END_TEST

static void op_attr_fail(fx_t *f, unsigned cmd) {
  job_t j;
  bim_tbc_t p[4];
  job_init(&j, cmd, 1);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Base", ty(f, "BaseT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = ta("id", attr_simple(ty(f, "BaseT"), "id"));
  job_path(&j, 4, p);
  j.values[0] = "again";
  apply_fail(f, &j, "LE400306");
}

static void fail_child(fx_t *f, unsigned cmd, const char *name, const bim_type_t *type, uint64_t pos, bim_node_t *n) {
  job_t j;
  bim_tbc_t p[3];
  job_init(&j, cmd, 1);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te(name, type);
  job_path(&j, 3, p);
  j.pos[2] = pos;
  j.payloads[0] = n;
  apply_fail(f, &j, "LE400306");
}

START_TEST(fuu_apply_state_errors) {
  fx_t f;
  job_t j;
  bim_tbc_t p[5];
  fx_init(&f, 0, 0);

  job_init(&j, BIM_CMD_DELETE, 1);
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(&f, "Box"));
  job_path(&j, 2, p);
  apply_fail(&f, &j, "LE400306");
  job_init(&j, BIM_CMD_ADD, 1);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("Base", ty(&f, "BaseT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = ta("id", attr_simple(ty(&f, "BaseT"), "id"));
  job_path(&j, 4, p);
  j.values[0] = "v";
  rt(&f, &j);
  ck_assert_str_eq(f.st.doc->name, "Box");
  ck_assert_str_eq(f.st.doc->kids[0]->name, "Base");

  job_init(&j, BIM_CMD_ADD, 1);
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(&f, "Box"));
  job_path(&j, 2, p);
  j.payloads[0] = bim_node_new("Box");
  apply_fail(&f, &j, "LE400306");
  job_init(&j, BIM_CMD_ADD, 1);
  p[1] = te("Other", gty(&f, "Other"));
  job_path(&j, 2, p);
  j.payloads[0] = bim_node_new("Other");
  apply_fail(&f, &j, "LE400306");

  op_attr_fail(&f, BIM_CMD_ADD);
  {
    bim_tbc_t d[3];
    d[0] = te("Box", gty(&f, "Box"));
    d[1] = te("Base", ty(&f, "BaseT"));
    d[2] = tb(BIM_TBC_END, NULL, NULL);
    op_attr(&f, BIM_CMD_DELETE, d, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), NULL);
  }
  op_attr_fail(&f, BIM_CMD_REPLACE);

  fail_child(&f, BIM_CMD_REPLACE, "Few", ty(&f, "EntryT"), 0, entry("Few", "a", NULL));
  fail_child(&f, BIM_CMD_DELETE, "Few", ty(&f, "EntryT"), 0, NULL);
  add_child(&f, BIM_CMD_ADD, "Few", ty(&f, "EntryT"), 0, entry("Few", "a", NULL));
  fail_child(&f, BIM_CMD_ADD, "Few", ty(&f, "EntryT"), 0, entry("Few", "b", NULL));

  add_child(&f, BIM_CMD_ADD, "Cho", ty(&f, "ChoT"), 0, cho());
  {
    bim_tbc_t c[4];
    c[0] = te("Box", gty(&f, "Box"));
    c[1] = te("Cho", ty(&f, "ChoT"));
    c[2] = tb(BIM_TBC_END, NULL, NULL);
    c[3] = te("B", kid_type(ty(&f, "ChoT"), "B"));
    job_init(&j, BIM_CMD_ADD, 1);
    job_path(&j, 4, c);
    j.payloads[0] = bim_node_new("B");
    apply_fail(&f, &j, "LE400306");
  }

  job_init(&j, BIM_CMD_DELETE, 1);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("Mix", ty(&f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = te("Em", kid_type(ty(&f, "MixT"), "Em"));
  job_path(&j, 4, p);
  apply_fail(&f, &j, "LE400306");
  fx_free(&f);
}
END_TEST

START_TEST(fuu_any_operands) {
  fx_t f;
  job_t j;
  bim_tbc_t p[3];
  bim_tbc_t a[3];
  fx_init(&f, 0, 0);
  add_box(&f);
  add_child(&f, BIM_CMD_ADD, "Other", gty(&f, "Other"), 0, entry("Other", "o", NULL));
  ck_assert_str_eq(f.st.doc->kids[0]->name, "Other");
  a[0] = te("Box", gty(&f, "Box"));
  a[1] = tb(BIM_TBC_END, NULL, NULL);
  op_attr(&f, BIM_CMD_ADD, a, 2, "gattr", &bim_xs_string, "gv");
  ck_assert_str_eq(bim_node_attr(f.st.doc, "gattr"), "gv");
  op_attr(&f, BIM_CMD_REPLACE, a, 2, "gattr", &bim_xs_string, "gw");
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = ta("nosuch", NULL);
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.values[0] = "x";
  wr_fail(&f, &j, "LE400304");
  p[2] = te("Unknown", ty(&f, "EntryT"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("Unknown");
  wr_fail(&f, &j, "LE400304");
  fx_free(&f);
}
END_TEST

static void implicit(int insert, int n, uint64_t victim) {
  fx_t f;
  job_t j;
  bim_tbc_t p[3];
  bim_node_t *b = bim_node_new("Box");
  fx_init(&f, insert, 0);
  for (int i = 0; i < n; i++) ck_assert_ptr_nonnull(bim_node_add_new(b, "Many", NULL));
  ck_assert_ptr_nonnull(bim_node_add_new(b, "Few", NULL));
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(&f, "Box"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 2, p);
  j.payloads[0] = b;
  rt(&f, &j);
  ck_assert_int_eq(f.st.doc->nkids, n + 1);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Many", ty(&f, "EntryT"));
  job_init(&j, BIM_CMD_DELETE, 1);
  job_path(&j, 3, p);
  j.pos[2] = victim;
  rt(&f, &j);
  ck_assert_int_eq(f.st.doc->nkids, n);
  fx_free(&f);
}

START_TEST(fuu_implicit_positions) {
  implicit(0, 5, 2);
  implicit(1, 5, 3ull << 61);
  implicit(1, 10, 11ull << 60);
  implicit(1, 10, 7ull << 60);
}
END_TEST

static void bits_after(bitwriter_t *bw, unsigned skip, bim_di_cpath_t *cp, uint64_t code) {
  size_t n;
  bitreader_t br;
  bitwriter_t out;
  const unsigned char *d = bitwriter_data(bw, &n);
  uint64_t v;
  uint64_t total = (uint64_t)bw->len * 8 + (uint64_t)bw->cur_bits;
  size_t on;
  bitreader_init(&br, d, n);
  ck_assert_int_eq(bitreader_skip(&br, skip), 0);
  bitwriter_init(&out);
  for (uint64_t i = skip; i < total; i++) {
    ck_assert_int_eq(bitreader_get(&br, 1, &v) || bitwriter_put(&out, v, 1), 0);
  }
  cp->nbits = total - skip;
  cp->code = code;
  d = bitwriter_data(&out, &on);
  cp->heap_bits = malloc(on ? on : 1);
  ck_assert_ptr_nonnull(cp->heap_bits);
  memcpy(cp->heap_bits, d, on);
  cp->bits = cp->heap_bits;
  bitwriter_free(&out);
}

START_TEST(fuu_context_path_table) {
  fx_t f;
  job_t j;
  bim_tbc_t p[3];
  bim_di_cpath_t cp;
  bim_err_t err;
  bitwriter_t bw;
  bim_fuu_t g;
  fx_init(&f, 0, 0);
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = ta("a", attr_simple(ty(&f, "BoxT"), "a"));
  job_init(&j, BIM_CMD_DELETE, 1);
  job_path(&j, 3, p);
  ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
  bits_after(&bw, 4 + (unsigned)bim_width(bim_di_nall(&f.di)), &cp, 2);
  bitwriter_free(&bw);
  f.di.cpt = 1;
  f.di.cpt_code_len = 2;
  f.di.cpaths = &cp;
  f.di.ncpaths = 1;

  job_init(&j, BIM_CMD_DELETE, 5);
  job_path(&j, 3, p);
  j.u.path_code = 2;
  rt(&f, &j);
  ck_assert_ptr_null(bim_node_attr(f.st.doc, "a"));

  j.u.path_code = 3;
  ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);

  cp.heap_bits[0] ^= 0x80;
  j.u.path_code = 2;
  ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  cp.heap_bits[0] ^= 0x80;
  sweep(&f, &j);

  f.di.cpt = 0;
  wr_fail(&f, &j, "LE400304");
  f.di.cpaths = NULL;
  f.di.ncpaths = 0;
  free(cp.heap_bits);
  fx_free(&f);
}
END_TEST

static const char schema_x[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:complexType name=\"XT\"><xs:sequence><xs:element name=\"V\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Ext\" type=\"XT\"/>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  bim_schema_t both[2];
  bim_ext_t ext;
  const bim_ext_t *tab[2];
} two_t;

static void two_init(fx_t *f, two_t *t, int external) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xml_doc_t *doc = NULL;
  memset(t, 0, sizeof *t);
  t->set = xsd_set_new();
  ck_assert_int_eq(xml_doc_parse(schema_x, strlen(schema_x), &doc, NULL), 0);
  if (xsd_set_add_doc(t->set, doc, "x.xsd") || xsd_set_resolve(t->set) || xsd_compile(t->set, &o, &t->c)) ck_abort_msg("%s", xsd_set_error(t->set));
  t->both[0] = f->c->schema;
  t->both[1] = t->c->schema;
  f->di.add_schema = 1;
  f->di.nadditional = 1;
  f->st.schemas = t->both;
  f->st.nschemas = 2;
  if (external) {
    t->ext.present = 1;
    t->ext.all = 1;
    t->tab[0] = &t->ext;
    f->st.castable = t->tab;
    f->st.substitutable = t->tab;
  }
}

static void two_free(two_t *t) { xsd_set_free(t->set); }

START_TEST(fuu_additional_schema) {
  fx_t f;
  two_t t;
  job_t j;
  bim_tbc_t p[4];
  bim_node_t *n;
  const bim_type_t *xt;
  fx_init(&f, 0, 0);
  two_init(&f, &t, 1);
  xt = xsd_compiled_type(t.c, NULL, "XT");
  ck_assert_ptr_nonnull(xt);

  job_init(&j, BIM_CMD_ADD, 1);
  p[0] = te("Ext", xsd_compiled_elem(t.c, NULL, "Ext")->type);
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("V", kid_type(xt, "V"));
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("V");
  ck_assert_int_eq(bim_node_set_text(j.payloads[0], "vv"), 0);
  rt(&f, &j);
  ck_assert_str_eq(f.st.doc->name, "Ext");
  bim_fuu_state_free(&f.st);

  add_box(&f);
  n = bim_node_new("Base");
  ck_assert_ptr_nonnull(bim_node_add_new(n, "V", "x"));
  add_child(&f, BIM_CMD_ADD, "Base", xt, 0, n);
  ck_assert_str_eq(f.st.doc->kids[0]->name, "Base");

  add_child(&f, BIM_CMD_ADD, "Ext", xt, 0, bim_node_new("Ext"));
  ck_assert_int_eq(f.st.doc->nkids, 2);

  p[0] = te("Box", gty(&f, "Box"));
  p[1] = te("Ext", xt);
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = te("V", kid_type(xt, "V"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 4, p);
  j.payloads[0] = bim_node_new("V");
  rt(&f, &j);
  j.u.schema = 5;
  wr_fail(&f, &j, "LE400304");
  two_free(&t);
  fx_free(&f);
}
END_TEST

START_TEST(fuu_additional_schema_internal) {
  fx_t f;
  two_t t;
  job_t j;
  bim_tbc_t p[3];
  fx_init(&f, 0, 0);
  two_init(&f, &t, 0);
  job_init(&j, BIM_CMD_ADD, 1);
  p[0] = te("Ext", xsd_compiled_elem(t.c, NULL, "Ext")->type);
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("V", kid_type(xsd_compiled_type(t.c, NULL, "XT"), "V"));
  job_path(&j, 3, p);
  j.u.schema = 1;
  j.payloads[0] = bim_node_new("V");
  rt(&f, &j);
  bim_fuu_state_free(&f.st);
  add_box(&f);
  j.payloads[0] = bim_node_new("V");
  j.u.schema = 0;
  f.di.add_schema = 0;
  wr_fail(&f, &j, "LE400304");
  two_free(&t);
  fx_free(&f);
}
END_TEST

typedef void (*mk_fn)(const fx_t *f, job_t *j);
typedef void (*mk_rw_fn)(fx_t *f, job_t *j);

static void mk_multi(const fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Many", ty(f, "EntryT"));
  job_init(j, BIM_CMD_ADD, 3);
  job_path(j, 3, p);
  j->u.npayloads = 3;
  j->pos[2] = f->di.insert ? rat(0) : 0;
  j->pos[5] = f->di.insert ? rat(1) : 1;
  j->pos[8] = f->di.insert ? rat(4) : 5;
  j->payloads[0] = entry("Many", "m0", NULL);
  j->payloads[1] = entry("Many", "m1", NULL);
  j->payloads[2] = entry("Many", "m5", NULL);
}

static void mk_typed(const fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("HeadAlt", ty(f, "DerivedT"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 3, p);
  j->payloads[0] = bim_node_new("HeadAlt");
  ck_assert_ptr_nonnull(bim_node_add_new(j->payloads[0], "Extra", "e"));
}

static void mk_attr(const fx_t *f, job_t *j) {
  bim_tbc_t p[4];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Base", ty(f, "BaseT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = ta("id", attr_simple(ty(f, "BaseT"), "id"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 4, p);
  j->values[0] = "id1";
}

static void oom_stages(int insert, mk_fn mk, mk_rw_fn mk_rw) {
  for (int stage = 0; stage < 3; stage++) {
    for (long n = 0;; n++) {
      fx_t f;
      job_t j;
      bim_err_t err;
      bitwriter_t bw;
      bim_fuu_t g;
      int parsed = 0;
      int hit;
      fx_init(&f, insert, 0);
      add_box(&f);
      add_child(&f, BIM_CMD_ADD, "Base", ty(&f, "BaseT"), 0, bim_node_new("Base"));
      if (mk) mk(&f, &j);
      else if (mk_rw) mk_rw(&f, &j);
      bitwriter_init(&bw);
      if (stage == 0) alloc_fail_at(n);
      if (bim_fuu_write(&f.st, &j.u, NULL, &f.sw, &bw, &err) && stage) ck_abort_msg("write %s", err.msg);
      hit = alloc_fail_end();
      if (stage && !hit) {
        if (stage == 1) alloc_fail_at(n);
        parsed = !parse_bw(&f, &bw, &g, &err);
        hit = stage == 1 ? alloc_fail_end() : 0;
        if (stage == 2) {
          ck_assert_int_eq(parsed, 1);
          alloc_fail_at(n);
          bim_fuu_apply(&f.st, &g, &err);
          hit = alloc_fail_end();
        }
        if (parsed) bim_fuu_free(&g);
      }
      bitwriter_free(&bw);
      job_done(&j);
      fx_free(&f);
      if (!hit) break;
    }
  }
}

START_TEST(fuu_allocation_failures) {
  oom_stages(0, mk_multi, NULL);
  oom_stages(1, mk_multi, NULL);
  oom_stages(0, mk_typed, NULL);
  oom_stages(0, mk_attr, NULL);
}
END_TEST

static uint64_t type_code(const fx_t *f, const char *name) {
  const bim_type_t *t = ty(f, name);
  for (int i = 0; i < f->c->schema.ntypes; i++)
    if (f->c->schema.types[i] == t) return (uint64_t)i;
  ck_abort_msg("type %s not in schema", name);
  return 0;
}

START_TEST(fuu_external_code_list) {
  fx_t f;
  two_t t;
  uint64_t codes[1];
  fx_init(&f, 0, 0);
  two_init(&f, &t, 1);
  codes[0] = type_code(&f, "BaseT");
  t.ext.all = 0;
  t.ext.codes = codes;
  t.ext.ncodes = 1;
  add_box(&f);
  add_child(&f, BIM_CMD_ADD, "Base", xsd_compiled_type(t.c, NULL, "XT"), 0, bim_node_new("Base"));
  ck_assert_int_eq(f.st.doc->nkids, 1);
  {
    bim_node_t *v = bim_node_new("Val");
    ck_assert_int_eq(bim_node_set_text(v, "t"), 0);
    add_child(&f, BIM_CMD_ADD, "Val", kid_type(ty(&f, "BoxT"), "Val"), 0, v);
  }
  codes[0] = type_code(&f, "EntryT");
  {
    job_t j;
    bim_tbc_t p[3];
    p[0] = te("Box", gty(&f, "Box"));
    p[1] = tb(BIM_TBC_END, NULL, NULL);
    p[2] = te("Head", xsd_compiled_type(t.c, NULL, "XT"));
    job_init(&j, BIM_CMD_ADD, 1);
    job_path(&j, 3, p);
    j.payloads[0] = bim_node_new("Head");
    wr_fail(&f, &j, "LE400304");
  }
  two_free(&t);
  fx_free(&f);
}
END_TEST

START_TEST(fuu_util_edges) {
  fx_t f;
  two_t t;
  bim_type_t dummy;
  bim_part_t part;
  memset(&dummy, 0, sizeof dummy);
  memset(&part, 0, sizeof part);
  part.max = 7;
  ck_assert_uint_eq(fuu_occ_max(NULL), 1);
  ck_assert_uint_eq(fuu_occ_max(&part), 7);
  ck_assert_int_eq(fuu_is_complex(NULL), 0);
  dummy.content = BIM_CONTENT_SIMPLE;
  ck_assert_int_eq(fuu_is_complex(&dummy), 0);
  dummy.nsbc = 1;
  ck_assert_int_eq(fuu_is_complex(&dummy), 1);
  fx_init(&f, 0, 0);
  two_init(&f, &t, 1);
  ck_assert_int_eq(fuu_external(&f.st, &dummy, 0), 0);
  ck_assert_int_eq(fuu_external(&f.st, &dummy, 1), 0);
  f.st.castable = NULL;
  ck_assert_int_eq(fuu_external(&f.st, &dummy, 0), 0);
  f.st.castable = t.tab;
  t.tab[0] = NULL;
  ck_assert_int_eq(fuu_external(&f.st, f.c->schema.types[0], 0), 0);
  t.tab[0] = &t.ext;
  t.ext.present = 0;
  ck_assert_int_eq(fuu_external(&f.st, f.c->schema.types[0], 0), 0);
  {
    bim_type_t big;
    bim_part_t top;
    bim_part_t *kids = calloc(300, sizeof *kids);
    bim_sbc_t *sbc = calloc(300, sizeof *sbc);
    ck_assert_ptr_nonnull(kids);
    ck_assert_ptr_nonnull(sbc);
    memset(&big, 0, sizeof big);
    memset(&top, 0, sizeof top);
    for (int i = 0; i < 300; i++) {
      kids[i].kind = BIM_PART_ELEM;
      sbc[i].kind = BIM_SBC_ELEM;
    }
    top.kind = BIM_PART_SEQ;
    top.kids = kids;
    top.nkids = 300;
    big.top = &top;
    big.sbc = sbc;
    big.nsbc = 300;
    ck_assert_ptr_nonnull(fuu_part_of(&big, 3));
    ck_assert_ptr_null(fuu_part_of(&big, 299));
    big.top = NULL;
    ck_assert_ptr_null(fuu_part_of(&big, 0));
    free(kids);
    free(sbc);
  }
  two_free(&t);
  fx_free(&f);
}
END_TEST

static int pcc(const fx_t *f, bim_tbc_kind_t k, const bim_type_t *pt, uint64_t pmax, int sel, int *w, int *mpc) {
  bim_tbc_t t = tb(k, "x", NULL);
  return bim_fuu_pc_coding(&f->st, &t, pt, pmax, sel, w, mpc);
}

static bim_part_t part(bim_part_kind_t kind, uint64_t max, const bim_part_t *kids, int n) {
  bim_part_t p;
  memset(&p, 0, sizeof p);
  p.kind = kind;
  p.max = max;
  p.kids = kids;
  p.nkids = n;
  return p;
}

START_TEST(fuu_position_code_selection) {
  fx_t f;
  bim_type_t pt;
  bim_part_t e1[2];
  bim_part_t top;
  bim_part_t nest[2];
  int w;
  int mpc;
  fx_init(&f, 0, 0);
  memset(&pt, 0, sizeof pt);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 1, &w, &mpc), BIM_PC_NONE);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ATTR, &pt, 5, 0, &w, &mpc), BIM_PC_NONE);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_NONE);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, UINT64_MAX, 0, &w, &mpc), BIM_PC_V5);
  ck_assert_int_eq(w, 65);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 3, 0, &w, &mpc), BIM_PC_BITS);
  ck_assert_int_eq(w, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 100, 0, &w, &mpc), BIM_PC_V5);

  f.di.mpc_only = 1;
  ck_assert_int_eq(pcc(&f, BIM_TBC_CHARDATA, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  ck_assert_int_eq(mpc, 1);
  e1[0] = part(BIM_PART_ELEM, 3, NULL, 0);
  e1[1] = part(BIM_PART_ELEM, 4, NULL, 0);
  top = part(BIM_PART_SEQ, 1, e1, 2);
  pt.top = &top;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  ck_assert_int_eq(w, 3);
  top = part(BIM_PART_CHOICE, 1, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  ck_assert_int_eq(w, 2);
  top = part(BIM_PART_CHOICE, 0, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  top = part(BIM_PART_CHOICE, UINT64_MAX / 2, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  e1[0] = part(BIM_PART_ELEM, UINT64_MAX, NULL, 0);
  top = part(BIM_PART_SEQ, 1, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  e1[0] = part(BIM_PART_ELEM, UINT64_MAX - 1, NULL, 0);
  e1[1] = part(BIM_PART_ELEM, UINT64_MAX - 1, NULL, 0);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  top = part(BIM_PART_ALL, 1, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  top = part(BIM_PART_ALL, UINT64_MAX / 2, e1, 3);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  top = part(BIM_PART_ALL, 0, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);

  pt.mixed = 1;
  e1[0] = part(BIM_PART_ELEM, 2, NULL, 0);
  e1[1] = part(BIM_PART_ELEM, 2, NULL, 0);
  top = part(BIM_PART_SEQ, 1, e1, 2);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  ck_assert_int_eq(w, 4);
  e1[0] = part(BIM_PART_ELEM, UINT64_MAX / 2 + 1, NULL, 0);
  e1[1] = part(BIM_PART_ELEM, 1, NULL, 0);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  e1[0] = part(BIM_PART_ELEM, UINT64_MAX, NULL, 0);
  top = part(BIM_PART_CHOICE, 1, e1, 1);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  f.di.insert = 1;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_RATIONAL);
  pt.top = NULL;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_RATIONAL);
  f.di.mpc_only = 0;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_RATIONAL);
  pt.mixed = 0;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_NONE);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 5, 0, &w, &mpc), BIM_PC_RATIONAL);

  f.di.insert = 0;
  top = part(BIM_PART_ALL, 1, e1, 1);
  pt.top = &top;
  ck_assert_int_eq(bim_fuu_uses_mpc(&f.st, &pt), 1);
  e1[0] = part(BIM_PART_ELEM, 4, NULL, 0);
  e1[1] = part(BIM_PART_ELEM, 3, NULL, 0);
  top = part(BIM_PART_CHOICE, 1, e1, 2);
  f.di.mpc_only = 1;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_BITS);
  e1[0] = part(BIM_PART_ELEM, 70000, NULL, 0);
  pt.mixed = 0;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_V5);
  f.di.mpc_only = 0;
  nest[0] = part(BIM_PART_ELEM, 1, NULL, 0);
  nest[1] = part(BIM_PART_CHOICE, 1, e1, 1);
  top = part(BIM_PART_SEQ, 1, nest, 2);
  pt.top = &top;
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_NONE);
  nest[1] = part(BIM_PART_SEQ, 2, e1, 1);
  ck_assert_int_eq(bim_fuu_uses_mpc(&f.st, &pt), 1);
  nest[0] = part(BIM_PART_ANY, 1, NULL, 0);
  nest[1] = part(BIM_PART_ELEM, 1, NULL, 0);
  ck_assert_int_eq(pcc(&f, BIM_TBC_ELEM, &pt, 1, 0, &w, &mpc), BIM_PC_NONE);
  fx_free(&f);
}
END_TEST

static int splice(bitwriter_t *in, uint64_t nbits, unsigned drop, const char *tail, int pad, bitwriter_t *out) {
  size_t n;
  bitreader_t br;
  uint64_t v;
  uint64_t total = nbits - drop;
  const unsigned char *d = bitwriter_data(in, &n);
  bitwriter_init(out);
  bitreader_init(&br, d, n);
  for (uint64_t i = 0; i < total; i++) ck_assert_int_eq(bitreader_get(&br, 1, &v) || bitwriter_put(out, v, 1), 0);
  while (pad && (total + strlen(tail)) % 8) {
    if ((total + strlen(tail)) % 2) {
      bitwriter_free(out);
      return -1;
    }
    ck_assert_int_eq(bitwriter_put(out, 1, 2), 0);
    total += 2;
  }
  for (const char *c = tail; *c; c++) ck_assert_int_eq(bitwriter_put(out, (uint64_t)(*c == '1'), 1), 0);
  return 0;
}

static void parse_expect_at(int line, fx_t *f, bitwriter_t *bw, int bits, const char *code) {
  bim_fuu_t g;
  bim_err_t err;
  size_t n;
  bim_bits_t b = {NULL, 0, NULL};
  b.bits = bitwriter_data(bw, &n);
  b.nbits = bits ? (uint64_t)bits : (uint64_t)n * 8;
  if (code) {
    ck_assert_msg(bim_fuu_parse(&f->st, &b, &g, &err) == -1, "line %d parsed", line);
    ck_assert_msg(!strcmp(err.code, code), "line %d code %s", line, err.code);
  } else if (bim_fuu_parse(&f->st, &b, &g, &err)) {
    ck_abort_msg("parse %s %s", err.code, err.msg);
  } else {
    bim_fuu_free(&g);
  }
}

#define parse_expect(f, bw, bits, code) parse_expect_at(__LINE__, f, bw, bits, code)

static void del_many(const fx_t *f, job_t *j, uint64_t pos) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Many", ty(f, "EntryT"));
  job_init(j, BIM_CMD_DELETE, 3);
  job_path(j, 3, p);
  j->pos[2] = pos;
}

static void increments(int insert) {
  fx_t f;
  job_t j;
  bim_err_t err;
  bitwriter_t bw;
  bitwriter_t s;
  uint64_t nb;
  uint64_t cand[8];
  int aligned = 0;
  for (int c = 0; c < 8; c++) cand[c] = insert ? rat((uint64_t)c * 5) : (uint64_t)c * 16;
  fx_init(&f, insert, 0);
  add_box(&f);
  for (int c = 0; c < 8; c++) {
    del_many(&f, &j, cand[c]);
    ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
    nb = (uint64_t)bw.len * 8 + (uint64_t)bw.cur_bits;
    if (!c) {
      splice(&bw, nb, 2, "11", 0, &s);
      parse_expect(&f, &s, 0, NULL);
      bitwriter_free(&s);
      splice(&bw, nb, 2, "0011", 0, &s);
      parse_expect(&f, &s, 0, "LE400304");
      bitwriter_free(&s);
      splice(&bw, nb, 2, "0000", 0, &s);
      parse_expect(&f, &s, 0, "LE400304");
      bitwriter_free(&s);
      splice(&bw, nb, 2, "10", 0, &s);
      parse_expect(&f, &s, 0, "LE400304");
      bitwriter_free(&s);
      splice(&bw, nb, 2, "000111", 0, &s);
      parse_expect(&f, &s, 0, NULL);
      bitwriter_free(&s);
    }
    if (!splice(&bw, nb, 2, "", 1, &s)) {
      parse_expect(&f, &s, 0, "LE400303");
      bitwriter_free(&s);
      aligned++;
    }
    if (!splice(&bw, nb, 2, "00", 1, &s)) {
      parse_expect(&f, &s, 0, "LE400303");
      bitwriter_free(&s);
      aligned++;
    }
    bitwriter_free(&bw);
  }
  ck_assert_int_gt(aligned, 0);
  fx_free(&f);
}

START_TEST(fuu_increment_errors) {
  increments(0);
  increments(1);
}
END_TEST

START_TEST(fuu_rational_off_table) {
  fx_t f;
  job_t j;
  bim_err_t err;
  bitwriter_t bw;
  bitwriter_t s;
  uint64_t nb;
  fx_init(&f, 1, 0);
  add_box(&f);
  del_many(&f, &j, 0x5555ULL << 40);
  ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
  nb = (uint64_t)bw.len * 8 + (uint64_t)bw.cur_bits;
  splice(&bw, nb, 2, "0111", 0, &s);
  parse_expect(&f, &s, 0, "LE400304");
  bitwriter_free(&s);
  bitwriter_free(&bw);
  del_many(&f, &j, 0);
  j.u.mode = 1;
  j.u.tbcs[2] = te("Em", kid_type(ty(&f, "MixT"), "Em"));
  j.u.tbcs[1] = te("Mix", ty(&f, "MixT"));
  j.u.tbcs[0] = te("Box", gty(&f, "Box"));
  j.u.ntbcs = 4;
  j.tbcs[2] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[3] = te("Em", kid_type(ty(&f, "MixT"), "Em"));
  j.pos[1] = rat(0);
  j.pos[3] = 0;
  ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
  parse_expect(&f, &bw, 0, "LE400304");
  bitwriter_free(&bw);
  fx_free(&f);
  ck_assert_uint_eq(fuu_rat_order(1ULL << 62), fuu_rat_order(1ULL << 62));
}
END_TEST

static void mix_em(const fx_t *f, job_t *j, unsigned cmd, unsigned mode, int n, const uint64_t *mix, const uint64_t *em) {
  bim_tbc_t p[4];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Mix", ty(f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = te("Em", kid_type(ty(f, "MixT"), "Em"));
  job_init(j, cmd, mode);
  job_path(j, 4, p);
  j->u.npayloads = (size_t)n;
  for (int i = 0; i < n; i++) {
    j->pos[i * 4 + 1] = mix[i];
    j->pos[i * 4 + 3] = em[i];
    if (cmd != BIM_CMD_DELETE) j->payloads[i] = entry("Em", "k", NULL);
  }
}

START_TEST(fuu_two_layer_increments) {
  for (int insert = 0; insert < 2; insert++) {
    fx_t f;
    job_t j;
    uint64_t mix[4];
    uint64_t em[4];
    fx_init(&f, insert, 0);
    add_box(&f);
    add_child(&f, BIM_CMD_ADD, "Mix", ty(&f, "MixT"), insert ? rat(0) : 0, bim_node_new("Mix"));
    add_child(&f, BIM_CMD_ADD, "Mix", ty(&f, "MixT"), insert ? rat(1) : 1, bim_node_new("Mix"));
    mix[0] = mix[1] = insert ? rat(0) : 0;
    mix[2] = mix[3] = insert ? rat(1) : 1;
    em[0] = insert ? rat(0) : 0;
    em[1] = insert ? rat(1) : 1;
    em[2] = insert ? rat(0) : 0;
    em[3] = insert ? rat(1) : 1;
    mix_em(&f, &j, BIM_CMD_ADD, 3, 4, mix, em);
    rt(&f, &j);
    ck_assert_int_eq(f.st.doc->nkids, 2);
    fx_free(&f);
  }
}
END_TEST

START_TEST(fuu_position_streams_truncated) {
  for (int insert = 0; insert < 2; insert++) {
    fx_t f;
    job_t j;
    uint64_t mix[1] = {insert ? rat(0) : 0};
    uint64_t em[1] = {insert ? rat(0) : 0};
    bim_tbc_t p[3];
    fx_init(&f, insert, 0);
    add_box(&f);
    del_many(&f, &j, insert ? rat(2) : 33);
    sweep(&f, &j);
    p[0] = te("Box", gty(&f, "Box"));
    p[1] = tb(BIM_TBC_END, NULL, NULL);
    p[2] = te("Few", ty(&f, "EntryT"));
    job_init(&j, BIM_CMD_DELETE, 1);
    job_path(&j, 3, p);
    j.pos[2] = insert ? rat(2) : 2;
    sweep(&f, &j);
    mix_em(&f, &j, BIM_CMD_DELETE, 3, 1, mix, em);
    sweep(&f, &j);
    fx_free(&f);
  }
}
END_TEST

START_TEST(fuu_position_field_truncated) {
  int cut = 0;
  for (int two = 0; two < 2; two++) {
    for (int v = 0; v < 3; v++) {
      fx_t f;
      two_t t;
      job_t j;
      bim_err_t err;
      bitwriter_t bw;
      uint64_t mix[1] = {1};
      uint64_t em[1] = {1};
      bim_tbc_t p[4];
      fx_init(&f, 0, 0);
      if (two) two_init(&f, &t, 0);
      add_box(&f);
      p[0] = te("Box", gty(&f, "Box"));
      p[1] = tb(BIM_TBC_END, NULL, NULL);
      p[2] = te(v ? "Cho" : "Few", ty(&f, v ? "ChoT" : "EntryT"));
      job_init(&j, BIM_CMD_DELETE, 1);
      job_path(&j, 3, p);
      j.pos[2] = 1;
      if (v == 2) mix_em(&f, &j, BIM_CMD_DELETE, 1, 1, mix, em);
      ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
      for (int drop = 1; drop <= 2; drop++) {
        uint64_t nb = (uint64_t)bw.len * 8 + (uint64_t)bw.cur_bits;
        if ((nb - (uint64_t)drop) % 8) continue;
        parse_expect(&f, &bw, (int)(nb - (uint64_t)drop), "LE400303");
        cut++;
      }
      bitwriter_free(&bw);
      if (two) two_free(&t);
      fx_free(&f);
    }
  }
  ck_assert_int_gt(cut, 0);
}
END_TEST

static void wfail(fx_t *f, unsigned cmd, unsigned mode, size_t n, const bim_tbc_t *p, bim_node_t *payload) {
  job_t j;
  job_init(&j, cmd, mode);
  job_path(&j, n, p);
  j.payloads[0] = payload;
  j.values[0] = "v";
  wr_fail(f, &j, "LE400304");
}

START_TEST(fuu_write_rejects_invalid_jobs) {
  fx_t f;
  job_t j;
  bim_tbc_t p[5] = {0};
  bim_tbc_t few;
  fx_init(&f, 0, 0);
  add_box(&f);
  few = te("Few", ty(&f, "EntryT"));

  job_init(&j, 0, 1);
  job_path(&j, 0, p);
  wr_fail(&f, &j, "LE400304");
  job_init(&j, 9, 1);
  wr_fail(&f, &j, "LE400304");

  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = few;
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.schema = 3;
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.schema = 0;
  j.u.mode = 0;
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.mode = 6;
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.mode = 1;
  j.u.ntbcs = 0;
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.ntbcs = 3;
  j.u.tbcs[0] = tb(BIM_TBC_END, NULL, NULL);
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.tbcs[0] = te("Box", gty(&f, "Box"));
  j.u.ntbcs = 1;
  wr_fail(&f, &j, "LE400304");
  j.payloads[0] = entry("Few", "a", NULL);
  j.u.ntbcs = 2;
  wr_fail(&f, &j, "LE400304");

  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  wfail(&f, BIM_CMD_ADD, 1, 2, p, NULL);
  p[0] = te("Zed", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = few;
  wfail(&f, BIM_CMD_ADD, 1, 3, p, entry("Few", "a", NULL));

  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_PARENT, NULL, NULL);
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = few;
  wfail(&f, BIM_CMD_ADD, 1, 4, p, entry("Few", "a", NULL));
  p[1] = ta("a", NULL);
  wfail(&f, BIM_CMD_ADD, 1, 4, p, entry("Few", "a", NULL));
  p[1] = te("Zed", ty(&f, "EntryT"));
  wfail(&f, BIM_CMD_ADD, 1, 4, p, entry("Few", "a", NULL));

  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = tb(BIM_TBC_CHARDATA, NULL, NULL);
  p[2].simple = &bim_xs_string;
  wfail(&f, BIM_CMD_ADD, 1, 3, p, NULL);
  p[2] = tb(BIM_TBC_SIMPLE, NULL, NULL);
  p[2].simple = &bim_xs_string;
  wfail(&f, BIM_CMD_ADD, 1, 3, p, NULL);

  p[2] = te("Few", ty(&f, "BaseT"));
  wfail(&f, BIM_CMD_ADD, 1, 3, p, bim_node_new("Few"));
  p[2] = te("Base", ty(&f, "EntryT"));
  wfail(&f, BIM_CMD_ADD, 1, 3, p, bim_node_new("Base"));

  p[2] = few;
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.u.npayloads = 2;
  j.payloads[0] = entry("Few", "a", NULL);
  j.payloads[1] = entry("Few", "b", NULL);
  wr_fail(&f, &j, "LE400304");
  job_init(&j, BIM_CMD_ADD, 3);
  job_path(&j, 3, p);
  j.u.npayloads = 2;
  j.payloads[0] = entry("Few", "a", NULL);
  j.payloads[1] = entry("Few", "b", NULL);
  j.pos[2] = j.pos[5] = 1;
  wr_fail(&f, &j, "LE400304");

  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.u.payloads = NULL;
  wr_fail(&f, &j, "LE400304");
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("Few");
  ck_assert_ptr_nonnull(bim_node_add_new(j.payloads[0], "Nope", NULL));
  wr_fail(&f, &j, "LE400304");
  p[2] = ta("a", attr_simple(ty(&f, "BoxT"), "a"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.u.values = NULL;
  wr_fail(&f, &j, "LE400304");

  f.di.aod = 1;
  job_init(&j, BIM_CMD_RESET, 1);
  j.u.reparam = 3;
  wr_fail(&f, &j, "LE400304");
  j.u.reparam = 0;
  wr_fail(&f, &j, "LE400304");
  f.di.aod = 0;

  f.di.mpegb = 0;
  job_init(&j, BIM_CMD_ADD, 5);
  job_path(&j, 3, p);
  wr_fail(&f, &j, "LE400304");
  fx_free(&f);
}
END_TEST

START_TEST(fuu_user_data_operand) {
  fx_t f;
  job_t j;
  bim_tbc_t p[2];
  fx_init(&f, 0, 0);
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 2, p);
  j.u.user_data = 1;
  rt(&f, &j);
  ck_assert_str_eq(f.st.doc->name, "Box");
  sweep(&f, &j);
  fx_free(&f);
}
END_TEST

START_TEST(fuu_write_external_failures) {
  fx_t f;
  two_t t;
  job_t j;
  bim_tbc_t p[3];
  bim_type_t stray;
  fx_init(&f, 0, 0);
  two_init(&f, &t, 1);
  add_box(&f);
  memset(&stray, 0, sizeof stray);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("HeadAlt", ty(&f, "DerivedT"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("HeadAlt");
  rt(&f, &j);
  wfail(&f, BIM_CMD_ADD, 1, 3, (bim_tbc_t[]){p[0], p[1], te("Zzz", ty(&f, "EntryT"))}, bim_node_new("Zzz"));
  wfail(&f, BIM_CMD_ADD, 1, 3, (bim_tbc_t[]){p[0], p[1], te("Base", &stray)}, bim_node_new("Base"));
  f.st.opts.lax_schema = 1;
  wfail(&f, BIM_CMD_ADD, 1, 3, (bim_tbc_t[]){p[0], p[1], te("Ext", xsd_compiled_elem(t.c, NULL, "Ext")->type)}, bim_node_new("Ext"));
  f.st.opts.lax_schema = -1;
  t.both[1].known = 0;
  wfail(&f, BIM_CMD_ADD, 1, 3, (bim_tbc_t[]){p[0], p[1], te("Ext", xsd_compiled_elem(t.c, NULL, "Ext")->type)}, bim_node_new("Ext"));
  t.both[1].known = 1;
  two_free(&t);
  fx_free(&f);
}
END_TEST

START_TEST(fuu_lax_attribute_operand) {
  fx_t f;
  two_t t;
  job_t j;
  bim_tbc_t p[3];
  bim_err_t err;
  bitwriter_t bw;
  fx_init(&f, 0, 0);
  two_init(&f, &t, 0);
  f.st.opts.lax_schema = 1;
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  for (unsigned cmd = BIM_CMD_ADD; cmd <= BIM_CMD_DELETE; cmd++) {
    p[2] = ta("custom", &bim_xs_string);
    p[2].owned = 1;
    job_init(&j, cmd, 1);
    job_path(&j, 3, p);
    j.values[0] = cmd == BIM_CMD_DELETE ? NULL : "lv";
    ck_assert_int_eq(wr(&f, &j.u, &bw, &err), 0);
    {
      bim_fuu_t g;
      ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), 0);
      ck_assert_int_eq(g.tbcs[2].owned, 1);
      ck_assert_str_eq(g.tbcs[2].name, "custom");
      ck_assert_int_eq(bim_fuu_apply(&f.st, &g, &err), 0);
      bim_fuu_free(&g);
    }
    bitwriter_free(&bw);
  }
  ck_assert_ptr_null(bim_node_attr(f.st.doc, "custom"));
  two_free(&t);
  fx_free(&f);
}
END_TEST

static void mk_any(const fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Other", gty(f, "Other"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 3, p);
  j->payloads[0] = entry("Other", "o", NULL);
}

static void mk_anyattr(const fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = ta("gattr", &bim_xs_string);
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 3, p);
  j->values[0] = "g";
}

static void mk_derived(const fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Base", ty(f, "DerivedT"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 3, p);
  j->payloads[0] = bim_node_new("Base");
}

static void mk_chardata(const fx_t *f, job_t *j) {
  bim_tbc_t p[4];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Mix", ty(f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = tb(BIM_TBC_CHARDATA, NULL, NULL);
  p[3].simple = &bim_xs_string;
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 4, p);
  j->values[0] = "text";
}

static void mk_mix_multi(fx_t *f, job_t *j) {
  uint64_t mix[2] = {f->di.insert ? rat(0) : 0, f->di.insert ? rat(1) : 1};
  uint64_t em[2] = {f->di.insert ? rat(0) : 0, f->di.insert ? rat(0) : 0};
  mix_em(f, j, BIM_CMD_ADD, 3, 2, mix, em);
}

START_TEST(fuu_allocation_failures_more_jobs) {
  oom_stages(0, mk_any, NULL);
  oom_stages(0, mk_anyattr, NULL);
  oom_stages(0, mk_derived, NULL);
  oom_stages(0, mk_chardata, NULL);
  oom_stages(0, NULL, mk_mix_multi);
  oom_stages(1, NULL, mk_mix_multi);
}
END_TEST

static void sweep2(fx_t *f, job_t *j) {
  bim_err_t err;
  bitwriter_t bw;
  bim_fuu_t g;
  bim_bits_t b = {NULL, 0, NULL};
  size_t n;
  unsigned char *copy;
  if (wr(f, &j->u, &bw, &err)) ck_abort_msg("write %s %s", err.code, err.msg);
  b.bits = bitwriter_data(&bw, &n);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  for (uint64_t x = 0; x < (uint64_t)n * 8 && n <= 8; x++)
    for (uint64_t y = x + 1; y < (uint64_t)n * 8; y++) {
      memcpy(copy, bitwriter_data(&bw, &n), n);
      copy[x / 8] ^= (unsigned char)(0x80u >> (x % 8));
      copy[y / 8] ^= (unsigned char)(0x80u >> (y % 8));
      b.bits = copy;
      b.nbits = (uint64_t)n * 8;
      if (!bim_fuu_parse(&f->st, &b, &g, &err)) bim_fuu_free(&g);
    }
  free(copy);
  bitwriter_free(&bw);
  sweep(f, j);
}

START_TEST(fuu_parse_sweeps_all_operand_kinds) {
  fx_t f;
  job_t j;
  bim_tbc_t p[4];
  uint64_t mix[2] = {0, 1};
  uint64_t em[2] = {0, 0};
  fx_init(&f, 0, 0);
  add_box(&f);
  add_child(&f, BIM_CMD_ADD, "Base", ty(&f, "BaseT"), 0, bim_node_new("Base"));
  add_child(&f, BIM_CMD_ADD, "Mix", ty(&f, "MixT"), 0, bim_node_new("Mix"));
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("HeadAlt", ty(&f, "DerivedT"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("HeadAlt");
  sweep2(&f, &j);
  job_done(&j);
  mk_any(&f, &j);
  sweep2(&f, &j);
  job_done(&j);
  mk_anyattr(&f, &j);
  sweep2(&f, &j);
  mk_derived(&f, &j);
  sweep2(&f, &j);
  job_done(&j);
  mk_chardata(&f, &j);
  sweep2(&f, &j);
  mix_em(&f, &j, BIM_CMD_ADD, 3, 2, mix, em);
  sweep2(&f, &j);
  job_done(&j);
  {
    bim_tbc_t d[3];
    d[0] = te("Box", gty(&f, "Box"));
    d[1] = te("Base", ty(&f, "BaseT"));
    d[2] = tb(BIM_TBC_END, NULL, NULL);
    op_attr(&f, BIM_CMD_ADD, d, 3, "id", attr_simple(ty(&f, "BaseT"), "id"), "z");
  }
  job_init(&j, BIM_CMD_ADD, 2);
  j.tbcs[0] = tb(BIM_TBC_PARENT, NULL, NULL);
  j.tbcs[1] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[2] = te("Few", ty(&f, "EntryT"));
  j.u.ntbcs = 3;
  j.payloads[0] = entry("Few", "r", NULL);
  sweep2(&f, &j);
  job_done(&j);
  job_init(&j, BIM_CMD_ADD, 2);
  j.tbcs[0] = tb(BIM_TBC_END, NULL, NULL);
  j.tbcs[1] = ta("id", attr_simple(ty(&f, "BaseT"), "id"));
  j.u.ntbcs = 2;
  j.values[0] = "x";
  sweep2(&f, &j);
  job_init(&j, BIM_CMD_ADD, 2);
  j.u.user_data = 1;
  j.tbcs[0] = tb(BIM_TBC_END, NULL, NULL);
  j.u.ntbcs = 1;
  sweep2(&f, &j);
  fx_free(&f);
}
END_TEST

START_TEST(fuu_parse_sweeps_additional_schema) {
  fx_t f;
  two_t t;
  job_t j;
  bim_tbc_t p[4];
  const bim_type_t *xt;
  fx_init(&f, 0, 0);
  two_init(&f, &t, 1);
  xt = xsd_compiled_type(t.c, NULL, "XT");
  f.st.opts.lax_schema = -1;
  add_box(&f);
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Base", xt);
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("Base");
  sweep2(&f, &j);
  job_done(&j);
  p[2] = te("Ext", xt);
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.payloads[0] = bim_node_new("Ext");
  sweep2(&f, &j);
  job_done(&j);
  p[0] = te("Ext", xt);
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("V", kid_type(xt, "V"));
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.u.schema = 1;
  j.payloads[0] = bim_node_new("V");
  sweep2(&f, &j);
  job_done(&j);
  f.st.opts.lax_schema = 1;
  p[0] = te("Box", gty(&f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = ta("custom", &bim_xs_string);
  p[2].owned = 1;
  job_init(&j, BIM_CMD_ADD, 1);
  job_path(&j, 3, p);
  j.values[0] = "lv";
  sweep2(&f, &j);
  two_free(&t);
  fx_free(&f);
}
END_TEST

static void put_bits(bitwriter_t *bw, uint64_t v, int n) { ck_assert_int_eq(bitwriter_put(bw, v, n), 0); }

START_TEST(fuu_deep_context_paths) {
  fx_t f;
  bitwriter_t bw;
  bim_err_t err;
  bim_fuu_t g;
  int tree = -1;
  fx_init(&f, 0, 0);
  for (int i = 0; i < f.c->schema.nelems; i++)
    if (!strcmp(f.c->schema.elems[i]->name, "Tree")) tree = i;
  ck_assert_int_ge(tree, 0);
  bitwriter_init(&bw);
  put_bits(&bw, BIM_CMD_ADD, 4);
  put_bits(&bw, 1, 3);
  put_bits(&bw, (uint64_t)tree, bim_width((uint64_t)f.c->schema.nelems + 1));
  for (int i = 0; i < 300; i++) put_bits(&bw, 1, 2);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  {
    size_t depth = 70;
    bim_tbc_t *p = calloc(depth + 3, sizeof *p);
    uint64_t *pos = calloc(depth + 3, sizeof *pos);
    bim_node_t *leaf = bim_node_new("Kid");
    job_t j;
    ck_assert_ptr_nonnull(p);
    ck_assert_ptr_nonnull(pos);
    p[0] = te("Tree", gty(&f, "Tree"));
    for (size_t i = 1; i <= depth; i++) p[i] = te("Kid", ty(&f, "NodeT"));
    p[depth + 1] = tb(BIM_TBC_END, NULL, NULL);
    p[depth + 2] = te("Kid", ty(&f, "NodeT"));
    job_init(&j, BIM_CMD_ADD, 1);
    j.u.tbcs = p;
    j.u.pos = pos;
    j.u.ntbcs = depth + 3;
    j.payloads[0] = leaf;
    apply_fail(&f, &j, "LE400306");
    free(p);
    free(pos);
  }
  fx_free(&f);
}
END_TEST

static void uq_raw(bim_di_instance_t *in, unsigned long vmax, int nbits) {
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

static uint64_t float_code(const fx_t *f) {
  const bim_simple_t *s = xsd_compiled_elem(f->c, NULL, "Speed")->type->simple;
  for (int i = 1; i < f->c->schema.ntypes; i++)
    if (f->c->schema.types[i]->simple == s && !f->c->schema.types[i]->nsbc) return (uint64_t)i - 1;
  ck_abort_msg("float type not in schema");
  return 0;
}

START_TEST(fuu_reparameterization_stream) {
  fx_t f;
  bim_di_instance_t in;
  bim_di_mapping_t map;
  bim_di_typeref_t tr[2];
  bim_decoderinit_t cfg;
  bim_fuu_t u;
  bim_fuu_t g;
  bim_err_t err;
  bitwriter_t bw;
  uint64_t inst = 0;
  char *uri = "urn:mpeg:mpegb:bim:SystemsAdvancedOptimisedDecodersCS:2007:2";
  fx_init(&f, 0, 0);
  f.di.aod = 1;
  f.di.naod_types = 1;
  f.di.aod_types = &uri;
  uq_raw(&in, 10, 4);
  tr[0].schema = tr[1].schema = 0;
  tr[0].type = tr[1].type = float_code(&f);
  memset(&map, 0, sizeof map);
  map.instances = &inst;
  map.ninstances = 1;
  map.types = tr;
  map.ntypes = 1;
  memset(&cfg, 0, sizeof cfg);
  cfg.instances = &in;
  cfg.ninstances = 1;
  cfg.mappings = &map;
  cfg.nmappings = 1;
  memset(&u, 0, sizeof u);
  u.command = BIM_CMD_RESET;
  u.config = &cfg;
  ck_assert_int_eq(bim_fuu_state_aod_init(&f.st, &err), 0);

  for (long n = 0;; n++) {
    int hit;
    int rc;
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_fuu_write(&f.st, &u, NULL, &f.sw, &bw, &err), 0);
    alloc_fail_at(n);
    rc = parse_bw(&f, &bw, &g, &err);
    hit = alloc_fail_end();
    if (!rc) bim_fuu_free(&g);
    bitwriter_free(&bw);
    if (!hit) break;
  }

  for (int rp = 1; rp <= 2; rp++) {
    memset(&u, 0, sizeof u);
    u.command = BIM_CMD_RESET;
    u.reparam = rp;
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_fuu_write(&f.st, &u, NULL, &f.sw, &bw, &err), 0);
    ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), 0);
    ck_assert_int_eq(g.reparam, rp);
    ck_assert_int_eq(bim_fuu_apply(&f.st, &g, &err), 0);
    bim_fuu_free(&g);
    bitwriter_free(&bw);
  }

  memset(&u, 0, sizeof u);
  u.command = BIM_CMD_RESET;
  u.config = &cfg;
  map.ntypes = 2;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_fuu_write(&f.st, &u, NULL, &f.sw, &bw, &err), 0);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), -1);
  ck_assert_str_eq(err.code, "LE400307");
  bitwriter_free(&bw);
  map.ntypes = 1;

  memset(&u, 0, sizeof u);
  u.command = BIM_CMD_RESET;
  u.config = &cfg;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_fuu_write(&f.st, &u, NULL, &f.sw, &bw, &err), 0);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), 0);
  ck_assert_int_eq(bim_fuu_apply(&f.st, &g, &err), 0);
  bim_fuu_free(&g);
  ck_assert_int_eq(parse_bw(&f, &bw, &g, &err), 0);
  bim_fuu_free(&g);
  bitwriter_free(&bw);

  free(in.raw);
  fx_free(&f);
}
END_TEST

static void mk_create_context(const fx_t *f, job_t *j) {
  bim_tbc_t p[4];
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Mix", ty(f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = te("Em", kid_type(ty(f, "MixT"), "Em"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 4, p);
  j->pos[1] = f->di.insert ? rat(0) : 0;
  j->pos[3] = f->di.insert ? rat(0) : 0;
  j->payloads[0] = entry("Em", "k", NULL);
}

static void mk_create_document(fx_t *f, job_t *j) {
  bim_tbc_t p[3];
  bim_fuu_state_free(&f->st);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Few", ty(f, "EntryT"));
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 3, p);
  j->pos[2] = 1;
  j->payloads[0] = entry("Few", "k", NULL);
}

static void mk_topmost(fx_t *f, job_t *j) {
  bim_tbc_t p[2];
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(f, "Box"));
  bim_fuu_state_free(&f->st);
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 2, p);
  j->payloads[0] = bim_node_new("Box");
}

static void mk_implicit_delete(fx_t *f, job_t *j) {
  bim_node_t *b = bim_node_new("Box");
  job_t t;
  bim_tbc_t p[3];
  bim_fuu_state_free(&f->st);
  for (int i = 0; i < 10; i++) ck_assert_ptr_nonnull(bim_node_add_new(b, "Many", NULL));
  p[0] = tb(BIM_TBC_END, NULL, NULL);
  p[1] = te("Box", gty(f, "Box"));
  job_init(&t, BIM_CMD_ADD, 1);
  job_path(&t, 2, p);
  t.payloads[0] = b;
  rt(f, &t);
  p[0] = te("Box", gty(f, "Box"));
  p[1] = tb(BIM_TBC_END, NULL, NULL);
  p[2] = te("Many", ty(f, "EntryT"));
  job_init(j, BIM_CMD_DELETE, 1);
  job_path(j, 3, p);
  j->pos[2] = f->di.insert ? 7ULL << 60 : 4;
}

static void mk_chardata_new(fx_t *f, job_t *j) {
  bim_tbc_t p[4];
  add_child(f, BIM_CMD_ADD, "Mix", ty(f, "MixT"), 1, bim_node_new("Mix"));
  p[0] = te("Box", gty(f, "Box"));
  p[1] = te("Mix", ty(f, "MixT"));
  p[2] = tb(BIM_TBC_END, NULL, NULL);
  p[3] = tb(BIM_TBC_CHARDATA, NULL, NULL);
  p[3].simple = &bim_xs_string;
  job_init(j, BIM_CMD_ADD, 1);
  job_path(j, 4, p);
  j->pos[1] = 1;
  j->pos[3] = 1;
  j->values[0] = "text";
}

START_TEST(fuu_apply_allocation_failures) {
  oom_stages(0, mk_create_context, NULL);
  oom_stages(1, mk_create_context, NULL);
  oom_stages(0, NULL, mk_create_document);
  oom_stages(0, NULL, mk_topmost);
  oom_stages(0, NULL, mk_implicit_delete);
  oom_stages(1, NULL, mk_implicit_delete);
  oom_stages(0, NULL, mk_chardata_new);
}
END_TEST

static Suite *fuu_paths_suite(void) {
  Suite *s = suite_create("bim_fuu_paths");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fuu_position_code_kinds);
  tcase_add_test(tc, fuu_attribute_and_simple_operands);
  tcase_add_test(tc, fuu_type_and_substitution_codes);
  tcase_add_test(tc, fuu_relative_paths);
  tcase_add_test(tc, fuu_multi_payload_positions);
  tcase_add_test(tc, fuu_parse_sweeps);
  tcase_add_test(tc, fuu_apply_state_errors);
  tcase_add_test(tc, fuu_any_operands);
  tcase_add_test(tc, fuu_implicit_positions);
  tcase_add_test(tc, fuu_context_path_table);
  tcase_add_test(tc, fuu_additional_schema);
  tcase_add_test(tc, fuu_additional_schema_internal);
  tcase_add_test(tc, fuu_allocation_failures);
  tcase_add_test(tc, fuu_external_code_list);
  tcase_add_test(tc, fuu_util_edges);
  tcase_add_test(tc, fuu_position_code_selection);
  tcase_add_test(tc, fuu_increment_errors);
  tcase_add_test(tc, fuu_rational_off_table);
  tcase_add_test(tc, fuu_two_layer_increments);
  tcase_add_test(tc, fuu_position_field_truncated);
  tcase_add_test(tc, fuu_write_rejects_invalid_jobs);
  tcase_add_test(tc, fuu_user_data_operand);
  tcase_add_test(tc, fuu_write_external_failures);
  tcase_add_test(tc, fuu_lax_attribute_operand);
  tcase_add_test(tc, fuu_allocation_failures_more_jobs);
  tcase_add_test(tc, fuu_parse_sweeps_all_operand_kinds);
  tcase_add_test(tc, fuu_parse_sweeps_additional_schema);
  tcase_add_test(tc, fuu_deep_context_paths);
  tcase_add_test(tc, fuu_reparameterization_stream);
  tcase_add_test(tc, fuu_apply_allocation_failures);
  tcase_add_test(tc, fuu_position_streams_truncated);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fuu_paths_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
