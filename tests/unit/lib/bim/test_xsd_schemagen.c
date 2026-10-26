/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"

#define XS "xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "

static const char doc_z[] =
  "<xs:schema " XS "xmlns:z=\"urn:z\" targetNamespace=\"urn:z\" elementFormDefault=\"qualified\">"
  "<xs:simpleType name=\"ZT\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:element name=\"ZE\" type=\"xs:string\"/>"
  "<xs:attribute name=\"za\" type=\"xs:string\"/>"
  "</xs:schema>";

static const char doc_rich[] =
  "<xs:schema " XS "xmlns:t=\"urn:x\" xmlns:z=\"urn:z\" targetNamespace=\"urn:x\" elementFormDefault=\"qualified\">"
  "<xs:import namespace=\"urn:z\"/>"
  "<xs:simpleType name=\"Str\"><xs:restriction base=\"xs:string\"><xs:length value=\"3\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Str2\"><xs:restriction base=\"xs:string\"><xs:minLength value=\"1\"/><xs:maxLength value=\"9\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Num\"><xs:restriction base=\"xs:int\"><xs:minExclusive value=\"0\"/><xs:maxExclusive value=\"10\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Lst\"><xs:list itemType=\"t:Num\"/></xs:simpleType>"
  "<xs:simpleType name=\"Uni\"><xs:union memberTypes=\"t:Num z:ZT\"/></xs:simpleType>"
  "<xs:attribute name=\"ga\" type=\"xs:string\"/>"
  "<xs:attribute name=\"gb\" type=\"xs:int\" default=\"4\"/>"
  "<xs:attributeGroup name=\"AG\"><xs:attribute name=\"ag\" type=\"xs:int\" use=\"required\"/><xs:anyAttribute/></xs:attributeGroup>"
  "<xs:group name=\"G\"><xs:sequence><xs:element name=\"GE\"><xs:complexType><xs:sequence><xs:element name=\"Q\" type=\"xs:string\"/></xs:sequence></xs:complexType></xs:element></xs:sequence></xs:group>"
  "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"b\" type=\"xs:string\" use=\"optional\"/><xs:attribute name=\"c\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"Restr\"><xs:complexContent><xs:restriction base=\"t:Base\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\" maxOccurs=\"2\"/></xs:sequence>"
  "<xs:attribute name=\"b\" use=\"prohibited\"/><xs:attribute name=\"c\" type=\"xs:string\" use=\"required\"/></xs:restriction></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"UsesG1\"><xs:group ref=\"t:G\"/></xs:complexType>"
  "<xs:complexType name=\"UsesG2\"><xs:sequence><xs:group ref=\"t:G\"/><xs:element ref=\"z:ZE\"/><xs:element name=\"ZT\" type=\"z:ZT\"/></xs:sequence>"
  "<xs:attribute ref=\"t:ga\"/><xs:attributeGroup ref=\"t:AG\"/></xs:complexType>"
  "<xs:complexType name=\"AllT\"><xs:all><xs:element name=\"P\" type=\"xs:string\"/><xs:element name=\"N\" type=\"xs:string\" nillable=\"true\" minOccurs=\"0\"/></xs:all></xs:complexType>"
  "<xs:complexType name=\"ChoT\"><xs:choice maxOccurs=\"unbounded\"><xs:element ref=\"t:Glob\"/><xs:any/></xs:choice></xs:complexType>"
  "<xs:complexType name=\"EmptyT\"/>"
  "<xs:complexType name=\"MixT\" mixed=\"true\"><xs:sequence><xs:element name=\"M\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"TxtT\"><xs:simpleContent><xs:extension base=\"t:Str\"><xs:attribute name=\"u\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:element name=\"Glob\" type=\"t:Base\"/>"
  "<xs:element name=\"Inline\"><xs:complexType><xs:sequence><xs:element name=\"I\" type=\"xs:string\"/></xs:sequence></xs:complexType></xs:element>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *es;
  const xsd_compiled_t *ec;
  xsd_set_t *s;
  const xsd_compiled_t *c;
  bim_err_t err;
  bim_node_t *tree;
} gx_t;

static void gx_enc(gx_t *g) {
  memset(g, 0, sizeof *g);
  ck_assert_int_eq(xsd_encoding_compile(&g->es, &g->ec), 0);
}

static void gx_load(gx_t *g, const char *const *docs, int n) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  g->s = xsd_set_new();
  ck_assert_ptr_nonnull(g->s);
  for (int i = 0; i < n; i++) {
    xml_doc_t *doc = NULL;
    ck_assert_int_eq(xml_doc_parse(docs[i], strlen(docs[i]), &doc, NULL), 0);
    if (xsd_set_add_doc(g->s, doc, "g.xsd")) ck_abort_msg("%s %s", xsd_set_error_code(g->s), xsd_set_error(g->s));
  }
  if (xsd_set_resolve(g->s) || xsd_compile(g->s, &o, &g->c)) ck_abort_msg("%s %s", xsd_set_error_code(g->s), xsd_set_error(g->s));
}

static void gx_free(gx_t *g) {
  bim_node_free(g->tree);
  xsd_set_free(g->s);
  xsd_set_free(g->es);
}

static int gx_gen(gx_t *g, const char *tns, const char *const *imp, int nimp, const xsd_compiled_t *known) {
  xsd_suu_src_t src;
  memset(&src, 0, sizeof src);
  bim_node_free(g->tree);
  g->tree = NULL;
  src.set = g->s;
  src.tns = tns;
  src.imported = imp;
  src.nimported = nimp;
  src.known = known;
  return xsd_suu_from_set(g->ec, &src, &g->tree, &g->err);
}

static const char *const imp_both[] = {XSD_NS, "urn:z"};
static const char *const imp_xs[] = {XSD_NS};

START_TEST(schemagen_rich_schema_round_trips) {
  gx_t g;
  bitwriter_t bw;
  bim_node_t *back = NULL;
  const unsigned char *p;
  size_t n;
  const char *docs[2] = {doc_rich, doc_z};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  if (gx_gen(&g, "urn:x", imp_both, 2, g.c)) ck_abort_msg("%s %s", g.err.code, g.err.msg);
  bitwriter_init(&bw);
  if (xsd_suu_encode(g.ec, g.tree, &bw, &g.err)) ck_abort_msg("%s %s", g.err.code, g.err.msg);
  p = bitwriter_data(&bw, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), 0);
  bim_node_free(back);
  bitwriter_free(&bw);
  gx_free(&g);
}
END_TEST

static void expect_gen_fail(const char *main_doc, const char *const *imp, int nimp, int use_enc_known, const char *what) {
  gx_t g;
  const char *docs[2] = {main_doc, doc_z};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  ck_assert_int_eq(gx_gen(&g, "urn:x", imp, nimp, use_enc_known ? g.ec : g.c), -1);
  ck_assert_str_eq(g.err.code, "LE400302");
  ck_assert_msg(strstr(g.err.msg, what) != NULL, "%s", g.err.msg);
  gx_free(&g);
}

#define SCH(body) "<xs:schema " XS "xmlns:t=\"urn:x\" xmlns:z=\"urn:z\" targetNamespace=\"urn:x\" elementFormDefault=\"qualified\"><xs:import namespace=\"urn:z\"/>" body "</xs:schema>"

START_TEST(schemagen_rejects_unrepresentable_models) {
  expect_gen_fail(SCH("<xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"a+\"/></xs:restriction></xs:simpleType>"), imp_both, 2, 0, "facet");
  expect_gen_fail(SCH("<xs:complexType name=\"M\" mixed=\"true\"/>"), imp_both, 2, 0, "mixed");
  expect_gen_fail(SCH("<xs:attribute name=\"gf\" type=\"xs:string\" fixed=\"x\"/>"), imp_both, 2, 0, "fixed");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:attribute name=\"f\" type=\"xs:string\" fixed=\"x\"/></xs:complexType>"), imp_both, 2, 0, "fixed");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:attribute ref=\"z:za\"/></xs:complexType>"), imp_both, 2, 0, "another namespace");
  expect_gen_fail(SCH("<xs:complexType name=\"A\" abstract=\"true\"/>"), imp_both, 2, 0, "abstract");
  expect_gen_fail(SCH("<xs:element name=\"N\" type=\"xs:string\" nillable=\"true\"/>"), imp_both, 2, 0, "nillable");
  expect_gen_fail(SCH("<xs:element name=\"H\" type=\"xs:string\"/><xs:element name=\"M\" substitutionGroup=\"t:H\"/>"), imp_both, 2, 0, "heads");
  expect_gen_fail(SCH("<xs:element name=\"M\" substitutionGroup=\"t:H\"/><xs:element name=\"H\" type=\"xs:string\"/>"), imp_both, 2, 0, "substitution");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:sequence><xs:element name=\"E\" type=\"z:ZT\"/></xs:sequence></xs:complexType>"), imp_xs, 1, 0, "not imported");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:sequence><xs:element name=\"E\" type=\"z:ZT\"/></xs:sequence></xs:complexType>"), imp_both, 2, 1, "unknown");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:sequence><xs:element ref=\"z:ZE\"/></xs:sequence></xs:complexType>"), imp_xs, 1, 0, "not imported");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:sequence><xs:element ref=\"z:ZE\"/></xs:sequence></xs:complexType>"), imp_both, 2, 1, "unknown");
  expect_gen_fail(SCH("<xs:complexType name=\"C\"><xs:sequence><xs:element name=\"E\" type=\"xs:string\"/></xs:sequence></xs:complexType>"), NULL, 0, 0, "not imported");
}
END_TEST

START_TEST(schemagen_encode_rejects_foreign_tree) {
  gx_t g;
  bitwriter_t bw;
  bim_node_t *t = bim_node_new("schema");
  gx_enc(&g);
  t->ns = XSD_ENCODING_NS;
  ck_assert_ptr_nonnull(bim_node_add_new(t, "Bogus", NULL));
  bitwriter_init(&bw);
  ck_assert_int_eq(xsd_suu_encode(g.ec, t, &bw, &g.err), -1);
  ck_assert_str_eq(g.err.code, "LE400302");
  bitwriter_free(&bw);
  bim_node_free(t);
  xsd_set_free(g.es);
}
END_TEST

START_TEST(schemagen_allocation_failures) {
  gx_t g;
  const char *docs[2] = {doc_rich, doc_z};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  for (long n = 0;; n++) {
    int hit;
    int rc;
    alloc_fail_at(n);
    rc = gx_gen(&g, "urn:x", imp_both, 2, g.c);
    hit = alloc_fail_end();
    ck_assert_int_eq(rc == 0, !hit);
    if (!hit) break;
  }
  for (long n = 0;; n++) {
    bitwriter_t bw;
    int hit;
    bitwriter_init(&bw);
    alloc_fail_at(n);
    xsd_suu_encode(g.ec, g.tree, &bw, &g.err);
    hit = alloc_fail_end();
    bitwriter_free(&bw);
    if (!hit) break;
  }
  gx_free(&g);
}
END_TEST

static const bim_node_t *find_node(const bim_node_t *n, const char *name, int *nth) {
  if (n->name && !strcmp(n->name, name) && (*nth)-- == 0) return n;
  for (int i = 0; i < n->nkids; i++) {
    const bim_node_t *r = find_node(n->kids[i], name, nth);
    if (r) return r;
  }
  return NULL;
}

static bim_node_t *node_at(gx_t *g, const char *name, int nth) {
  const bim_node_t *n = find_node(g->tree, name, &nth);
  ck_assert_msg(n != NULL, "no node %s", name);
  return (bim_node_t *)n;
}

static void drop_child(bim_node_t *p, const char *name) {
  for (int i = 0; i < p->nkids; i++)
    if (p->kids[i]->name && !strcmp(p->kids[i]->name, name)) {
      bim_node_free(p->kids[i]);
      memmove(&p->kids[i], &p->kids[i + 1], (size_t)(p->nkids - i - 1) * sizeof *p->kids);
      p->nkids--;
      return;
    }
  ck_abort_msg("no child %s", name);
}

static void rm_attr(bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nattrs; i++)
    if (!strcmp(n->attrs[i].name, name)) {
      free(n->attrs[i].value);
      memmove(&n->attrs[i], &n->attrs[i + 1], (size_t)(n->nattrs - i - 1) * sizeof *n->attrs);
      n->nattrs--;
      return;
    }
  ck_abort_msg("no attr %s", name);
}

static int to_xsd(gx_t *g, char **text) {
  xsd_suu_t in;
  memset(&in, 0, sizeof in);
  in.tree = g->tree;
  in.tns = "urn:x";
  in.imported = imp_both;
  in.nimported = 2;
  in.known = g->c;
  return xsd_suu_to_xsd(&in, text, &g->err);
}

typedef void (*tree_mut)(gx_t *g);

static void m_type_noname(gx_t *g) { rm_attr(node_at(g, "complexType", 0), "name"); }
static void m_no_content(gx_t *g) { drop_child(node_at(g, "complexType", 0), "content"); }
static void m_typeid_text(gx_t *g) { bim_node_set_attr(node_at(g, "namedTypeReference", 0), "TypeID", "zz"); }
static void m_typeid_absent(gx_t *g) { rm_attr(node_at(g, "namedTypeReference", 0), "TypeID"); }
static void m_ns_range(gx_t *g) {
  bim_node_t *r = node_at(g, "namedTypeReference", 0);
  bim_node_set_attr(r, "NamespaceID", "99");
}
static void m_type_unresolved(gx_t *g) { bim_node_set_attr(node_at(g, "namedTypeReference", 0), "TypeID", "9999"); }
static void m_anon_unresolved(gx_t *g) { bim_node_set_attr(node_at(g, "anonymousTypeReference", 0), "idref", "zz"); }
static void m_particle_nocast(gx_t *g) { node_at(g, "particle", 0)->cast = NULL; }
static void m_particle_unknown(gx_t *g) { node_at(g, "particle", 0)->cast = xsd_compiled_type(g->ec, XSD_ENCODING_NS, "emptyContentModelType"); }
static void m_elemref_missing(gx_t *g) {
  bim_node_t *ref = node_at(g, "ref", 0);
  drop_child(ref, "namedElementReference");
}
static void m_elemref_noref(gx_t *g) { drop_child(node_at(g, "ref", 0)->parent, "ref"); }
static void m_facet_noname(gx_t *g) { rm_attr(node_at(g, "facet", 0), "name"); }
static void m_simple_nothing(gx_t *g) { drop_child(node_at(g, "simpleType", 0), "derivation"); }
static void m_base_anon(gx_t *g) { node_at(g, "baseTypeReference", 0)->kids[0]->name = "anonymousTypeReference"; }
static void m_elemid_text(gx_t *g) { bim_node_set_attr(node_at(g, "namedElementReference", 0), "ElementID", "9999"); }

START_TEST(schemaupdate_to_xsd_rejects_damaged_trees) {
  static const tree_mut muts[] = {m_type_noname, m_no_content, m_typeid_text, m_typeid_absent, m_ns_range, m_type_unresolved, m_anon_unresolved, m_particle_nocast,
                                  m_particle_unknown, m_elemref_missing, m_elemref_noref, m_facet_noname, m_simple_nothing, m_base_anon, m_elemid_text};
  for (size_t k = 0; k < sizeof muts / sizeof *muts; k++) {
    gx_t g;
    char *text = NULL;
    const char *docs[2] = {doc_rich, doc_z};
    gx_enc(&g);
    gx_load(&g, docs, 2);
    ck_assert_int_eq(gx_gen(&g, "urn:x", imp_both, 2, g.c), 0);
    muts[k](&g);
    ck_assert_msg(to_xsd(&g, &text) == -1, "mutation %zu accepted", k);
    ck_assert_str_eq(g.err.code, "LE400302");
    free(text);
    gx_free(&g);
  }
}
END_TEST

START_TEST(schemaupdate_to_xsd_emits_a_loadable_schema) {
  gx_t g;
  char *text = NULL;
  xml_doc_t *doc = NULL;
  const char *docs[2] = {doc_rich, doc_z};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  ck_assert_int_eq(gx_gen(&g, "urn:x", imp_both, 2, g.c), 0);
  ck_assert_int_eq(to_xsd(&g, &text), 0);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  xml_doc_free(doc);
  free(text);
  gx_free(&g);
}
END_TEST

static int au_with(gx_t *g, const bim_node_t *tree, unsigned reparam, const uint8_t *pre, size_t npre, int pre_bits, bitwriter_t *out) {
  const bim_elem_t *root = xsd_compiled_elem(g->ec, XSD_ENCODING_NS, "schema");
  bim_encode_opts_t eo;
  bitwriter_t fuu;
  strrepo_writer_t sw;
  const unsigned char *p;
  size_t n;
  int rc = -1;
  memset(&eo, 0, sizeof eo);
  eo.schemas = &g->ec->schema;
  eo.nschemas = 1;
  eo.mpegb = 1;
  eo.cast = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  bitwriter_init(&fuu);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bitwriter_put(&fuu, 1, 4) || bitwriter_put(&fuu, reparam, 2), 0);
  for (size_t i = 0; i < npre; i++) ck_assert_int_eq(bitwriter_put(&fuu, pre[i], pre_bits), 0);
  ck_assert_int_eq(bitwriter_put(&fuu, 1, 3) || bitwriter_put(&fuu, 1, 1), 0);
  ck_assert_int_eq(bim_encode_payload_opts(root->type, tree, &eo, &fuu, &sw), 0);
  p = bitwriter_data(&fuu, &n);
  bitwriter_init(out);
  rc = bitwriter_put_vluimsbf8(out, 1) || bitwriter_put_vluimsbf8(out, n) || bitwriter_put_bytes(out, p, n);
  bitwriter_free(&fuu);
  strrepo_writer_free(&sw);
  return rc;
}

START_TEST(schemaupdate_decode_access_unit_variants) {
  gx_t g;
  bitwriter_t au;
  bim_node_t *back = NULL;
  const unsigned char *p;
  size_t n;
  const char *docs[2] = {doc_rich, doc_z};
  static const uint8_t map_ok[] = {0x00, 0x01, 0x00, 0x00, 0x02, 0x05, 0x06};
  static const uint8_t inst_bad[] = {0x01};
  static const uint8_t map_cut[] = {0x00, 0x01, 0x00};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  ck_assert_int_eq(gx_gen(&g, "urn:x", imp_both, 2, g.c), 0);
  ck_assert_int_eq(au_with(&g, g.tree, 1, NULL, 0, 8, &au), 0);
  p = bitwriter_data(&au, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), 0);
  bim_node_free(back);
  bitwriter_free(&au);

  ck_assert_int_eq(au_with(&g, g.tree, 0, map_ok, sizeof map_ok, 8, &au), 0);
  p = bitwriter_data(&au, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), 0);
  bim_node_free(back);
  bitwriter_free(&au);

  ck_assert_int_eq(au_with(&g, g.tree, 0, inst_bad, sizeof inst_bad, 8, &au), 0);
  p = bitwriter_data(&au, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), -1);
  bitwriter_free(&au);

  ck_assert_int_eq(au_with(&g, g.tree, 0, map_cut, sizeof map_cut, 8, &au), 0);
  p = bitwriter_data(&au, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), -1);
  bitwriter_free(&au);

  ck_assert_int_eq(au_with(&g, g.tree, 3, NULL, 0, 8, &au), 0);
  p = bitwriter_data(&au, &n);
  ck_assert_int_eq(xsd_suu_decode(g.ec, p, n * 8, &back, &g.err), -1);
  bitwriter_free(&au);

  ck_assert_int_eq(au_with(&g, g.tree, 1, NULL, 0, 8, &au), 0);
  {
    unsigned char *copy;
    p = bitwriter_data(&au, &n);
    copy = malloc(n);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, p, n);
    ck_assert_int_eq(xsd_suu_decode(g.ec, copy, 0, &back, &g.err), -1);
    copy[0] = 2;
    ck_assert_int_eq(xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err), -1);
    copy[0] = 1;
    copy[1] = 0x7F;
    ck_assert_int_eq(xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err), -1);
    memcpy(copy, p, n);
    copy[2] ^= 0xF0;
    ck_assert_int_eq(xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err), -1);
    memcpy(copy, p, n);
    copy[2] = (unsigned char)((copy[2] & 0xF0) | 0x0F);
    (void)xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err);
    memcpy(copy, p, n);
    copy[3] ^= 0xFF;
    (void)xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err);
    memcpy(copy, p, n);
    copy[1] = 4;
    ck_assert_int_eq(xsd_suu_decode(g.ec, copy, n * 8, &back, &g.err), -1);
    free(copy);
  }
  bitwriter_free(&au);
  gx_free(&g);
}
END_TEST

START_TEST(schemaupdate_allocation_failures) {
  gx_t g;
  const char *docs[2] = {doc_rich, doc_z};
  gx_enc(&g);
  gx_load(&g, docs, 2);
  ck_assert_int_eq(gx_gen(&g, "urn:x", imp_both, 2, g.c), 0);
  for (long n = 0;; n++) {
    char *text = NULL;
    int hit;
    int rc;
    alloc_fail_at(n);
    rc = to_xsd(&g, &text);
    hit = alloc_fail_end();
    if (!rc) free(text);
    if (!hit) break;
  }
  {
    bitwriter_t au;
    const unsigned char *p;
    size_t len;
    bitwriter_init(&au);
    ck_assert_int_eq(xsd_suu_encode(g.ec, g.tree, &au, &g.err), 0);
    p = bitwriter_data(&au, &len);
    for (long n = 0;; n++) {
      bim_node_t *back = NULL;
      int hit;
      int rc;
      alloc_fail_at(n);
      rc = xsd_suu_decode(g.ec, p, len * 8, &back, &g.err);
      hit = alloc_fail_end();
      if (!rc) bim_node_free(back);
      if (!hit) break;
    }
    bitwriter_free(&au);
  }
  gx_free(&g);
}
END_TEST

static Suite *schemagen_suite(void) {
  Suite *s = suite_create("bim_xsd_schemagen");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, schemagen_rich_schema_round_trips);
  tcase_add_test(tc, schemagen_rejects_unrepresentable_models);
  tcase_add_test(tc, schemagen_encode_rejects_foreign_tree);
  tcase_add_test(tc, schemagen_allocation_failures);
  tcase_add_test(tc, schemaupdate_to_xsd_rejects_damaged_trees);
  tcase_add_test(tc, schemaupdate_to_xsd_emits_a_loadable_schema);
  tcase_add_test(tc, schemaupdate_decode_access_unit_variants);
  tcase_add_test(tc, schemaupdate_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(schemagen_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
