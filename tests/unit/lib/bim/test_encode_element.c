/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"

#define XSI "http://www.w3.org/2001/XMLSchema-instance"

static const char schema_el[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:attribute name=\"ga\" type=\"xs:string\"/>"
  "<xs:attribute name=\"gb\" type=\"xs:string\"/>"
  "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\"/></xs:sequence>"
  "<xs:attribute name=\"k\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"Derived\"><xs:complexContent><xs:extension base=\"Base\"><xs:sequence>"
  "<xs:element name=\"B\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"Other\"><xs:sequence><xs:element name=\"Z\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Head\" type=\"Base\"/>"
  "<xs:element name=\"Sub\" type=\"Base\" substitutionGroup=\"Head\"/>"
  "<xs:complexType name=\"RootType\"><xs:sequence>"
  "<xs:element ref=\"Head\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"
  "<xs:element name=\"N\" type=\"xs:string\" nillable=\"true\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Root\" type=\"RootType\"/>"
  "<xs:complexType name=\"PlainRoot\"><xs:sequence><xs:element name=\"P\" type=\"Other\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Plain\" type=\"PlainRoot\"/>"
  "<xs:complexType name=\"ReqT\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\"/><xs:element name=\"I\" type=\"xs:int\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"req\" type=\"xs:string\" use=\"required\"/><xs:attribute name=\"num\" type=\"xs:int\"/></xs:complexType>"
  "<xs:element name=\"Req\" type=\"ReqT\"/>"
  "<xs:complexType name=\"MixedType\" mixed=\"true\"><xs:sequence>"
  "<xs:element name=\"I\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Mix\" type=\"MixedType\"/>"
  "<xs:complexType name=\"ChoT\"><xs:sequence><xs:choice><xs:element name=\"A\" type=\"xs:string\"/><xs:element name=\"B\" type=\"xs:string\"/>"
  "<xs:sequence/></xs:choice><xs:element name=\"Tail\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Cho\" type=\"ChoT\"/>"
  "<xs:complexType name=\"ChoReq\"><xs:choice><xs:element name=\"A\" type=\"xs:string\"/><xs:element name=\"B\" type=\"xs:string\"/></xs:choice></xs:complexType>"
  "<xs:element name=\"ChoR\" type=\"ChoReq\"/>"
  "<xs:complexType name=\"AnyOther\"><xs:sequence><xs:any namespace=\"##other\" processContents=\"lax\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"AnyO\" type=\"AnyOther\"/>"
  "<xs:complexType name=\"AnyList\"><xs:sequence><xs:any namespace=\"urn:a ##local\" processContents=\"lax\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"known\" type=\"xs:string\"/><xs:anyAttribute processContents=\"lax\"/></xs:complexType>"
  "<xs:element name=\"AnyL\" type=\"AnyList\"/>"
  "<xs:complexType name=\"TxtT\"><xs:simpleContent><xs:extension base=\"xs:int\"><xs:attribute name=\"u\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:element name=\"Txt\" type=\"TxtT\"/>"
  "<xs:complexType name=\"BigAll\"><xs:all>BIGALL</xs:all></xs:complexType>"
  "<xs:element name=\"BigA\" type=\"BigAll\"/>"
  "<xs:complexType name=\"OkAll\"><xs:all>OKALL</xs:all></xs:complexType>"
  "<xs:element name=\"OkA\" type=\"OkAll\"/>"
  "<xs:element name=\"Inline\"><xs:complexType><xs:sequence><xs:element name=\"Q\" type=\"xs:string\"/></xs:sequence></xs:complexType></xs:element>"
  "</xs:schema>";

static const char schema_ext[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:attribute name=\"xa\" type=\"xs:string\"/>"
  "<xs:complexType name=\"XT\"><xs:sequence><xs:element name=\"V\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Ext\" type=\"XT\"/>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  xsd_set_t *set2;
  const xsd_compiled_t *c2;
  bim_schema_t both[2];
} sc_t;

static char *subst_all(const char *s, const char *key, const char *val) {
  const char *at = strstr(s, key);
  size_t n = strlen(s) - strlen(key) + strlen(val) + 1;
  char *out = malloc(n);
  ck_assert_ptr_nonnull(out);
  memcpy(out, s, (size_t)(at - s));
  strcpy(out + (at - s), val);
  strcpy(out + (at - s) + strlen(val), at + strlen(key));
  return out;
}

static xsd_set_t *compile_doc(const char *text, const xsd_compiled_t **c) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xsd_set_t *s = xsd_set_new();
  xml_doc_t *doc = NULL;
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  if (xsd_set_add_doc(s, doc, "e.xsd") || xsd_set_resolve(s) || xsd_compile(s, &o, c)) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
  return s;
}

static char *all_elems(int n) {
  char *out = calloc((size_t)n * 64 + 1, 1);
  ck_assert_ptr_nonnull(out);
  for (int i = 0; i < n; i++) {
    char one[80];
    snprintf(one, sizeof one, "<xs:element name=\"E%d\" type=\"xs:string\" minOccurs=\"0\"/>", i);
    strcat(out, one);
  }
  return out;
}

static void sc_init(sc_t *s, int with_ext) {
  char *big = all_elems(65);
  char *ok = all_elems(64);
  char *a = subst_all(schema_el, "BIGALL", big);
  char *b = subst_all(a, "OKALL", ok);
  memset(s, 0, sizeof *s);
  s->set = compile_doc(b, &s->c);
  free(big);
  free(ok);
  free(a);
  free(b);
  if (with_ext) {
    s->set2 = compile_doc(schema_ext, &s->c2);
    s->both[0] = s->c->schema;
    s->both[1] = s->c2->schema;
  }
}

static void sc_free(sc_t *s) {
  xsd_set_free(s->set);
  xsd_set_free(s->set2);
}

static const bim_type_t *ty(const sc_t *s, const char *n) {
  const bim_type_t *t = xsd_compiled_type(s->c, NULL, n);
  ck_assert_ptr_nonnull(t);
  return t;
}

static bim_node_t *nn(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, text) : bim_node_new(name);
  ck_assert_ptr_nonnull(n);
  if (!parent && text) ck_assert_int_eq(bim_node_set_text(n, text), 0);
  return n;
}

static bim_encode_opts_t eopts(const sc_t *s) {
  bim_encode_opts_t eo;
  memset(&eo, 0, sizeof eo);
  eo.schemas = &s->c->schema;
  eo.nschemas = 1;
  eo.mpegb = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  return eo;
}

static int enc(const bim_type_t *t, const bim_node_t *tree, const bim_encode_opts_t *eo) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  rc = bim_encode_payload_opts(t, tree, eo, &bw, &sw);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

static const bim_type_t *g_res_type;
static bim_node_t *g_res_node;
static int g_res_fail;

static int frag_resolve(void *ctx, const char *href, unsigned format, unsigned char **buf, size_t *len) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_encode_opts_t eo;
  const unsigned char *p;
  (void)ctx;
  (void)href;
  (void)format;
  if (g_res_fail || !g_res_type) return -1;
  memset(&eo, 0, sizeof eo);
  eo.mpegb = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) return -1;
  if (bim_encode_payload_opts(g_res_type, g_res_node, &eo, &bw, &sw)) {
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    return -1;
  }
  p = bitwriter_data(&bw, len);
  *buf = malloc(*len ? *len : 1);
  if (*buf) memcpy(*buf, p, *len);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return *buf ? 0 : -1;
}

static int dec_bits(const bim_type_t *t, const char *root, const bim_encode_opts_t *eo, const unsigned char *bits, size_t n, bim_node_t **out) {
  bitreader_t br;
  bim_decode_opts_t o;
  memset(&o, 0, sizeof o);
  o.schemas = eo->schemas;
  o.nschemas = eo->nschemas;
  o.mpegb = eo->mpegb;
  o.compat = eo->compat;
  o.lax_schema = eo->lax_schema;
  o.ext_subst = eo->ext_subst;
  o.frag_formats = (const unsigned char *)"\x01";
  o.nfrag_formats = 1;
  o.resolve = eo->fragref ? &frag_resolve : NULL;
  bitreader_init(&br, bits, n);
  return bim_decode_payload(t, root, &o, &br, NULL, out);
}

static void decode_sweep(const bim_type_t *t, const bim_node_t *tree, const bim_encode_opts_t *eo) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *p;
  unsigned char *copy;
  size_t n;
  bim_node_t *out = NULL;
  if (!eo->mpegb) return;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(t, tree, eo, &bw, &sw), 0);
  p = bitwriter_data(&bw, &n);
  copy = malloc(n ? n : 1);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  if (!dec_bits(t, tree->name, eo, copy, n, &out)) bim_node_free(out);
  for (size_t cut = 0; cut < n; cut++)
    if (!dec_bits(t, tree->name, eo, copy, cut, &out)) bim_node_free(out);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    if (!dec_bits(t, tree->name, eo, copy, n, &out)) bim_node_free(out);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  if (eo->nschemas == 2) {
    bim_schema_t unknown[2];
    bim_encode_opts_t eo2 = *eo;
    unknown[0] = eo->schemas[0];
    unknown[1] = eo->schemas[1];
    unknown[1].known = 0;
    eo2.schemas = unknown;
    if (!dec_bits(t, tree->name, &eo2, copy, n, &out)) bim_node_free(out);
    for (size_t bit = 0; bit < n * 8; bit++) {
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      if (!dec_bits(t, tree->name, &eo2, copy, n, &out)) bim_node_free(out);
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
  }
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = dec_bits(t, tree->name, eo, copy, n, &out);
    hit = alloc_fail_end();
    if (!rc) bim_node_free(out);
    if (!hit) break;
  }
  free(copy);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}

static void enc_ok_at(int line, const bim_type_t *t, bim_node_t *tree, const bim_encode_opts_t *eo) {
  ck_assert_msg(enc(t, tree, eo) == 0, "line %d rejected", line);
  decode_sweep(t, tree, eo);
  bim_node_free(tree);
}

static void enc_bad_at(int line, const bim_type_t *t, bim_node_t *tree, const bim_encode_opts_t *eo) {
  ck_assert_msg(enc(t, tree, eo) == -1, "line %d accepted", line);
  bim_node_free(tree);
}

#define enc_ok(t, tree, eo) enc_ok_at(__LINE__, t, tree, eo)
#define enc_bad(t, tree, eo) enc_bad_at(__LINE__, t, tree, eo)

START_TEST(encode_element_bad_arguments) {
  sc_t s;
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_node_t *r;
  sc_init(&s, 0);
  eo = eopts(&s);
  r = nn(NULL, "Plain", NULL);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(ty(&s, "PlainRoot"), r, NULL, &bw, &sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(ty(&s, "PlainRoot"), r, &eo, &bw, NULL), -1);
  eo.len_mode = 3;
  ck_assert_int_eq(bim_encode_payload_opts(ty(&s, "PlainRoot"), r, &eo, &bw, &sw), -1);
  eo.len_mode = -1;
  ck_assert_int_eq(bim_encode_payload_opts(ty(&s, "PlainRoot"), r, &eo, &bw, &sw), -1);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bim_node_free(r);
  sc_free(&s);
}
END_TEST

START_TEST(encode_element_rejects_bad_trees) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_node_t *r;
  bim_node_t *c;
  sc_init(&s, 0);
  eo = eopts(&s);
  for (int len = 0; len < 3; len++) {
    eo.len_mode = len;
    r = nn(NULL, "Req", NULL);
    nn(r, "A", "x");
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    ck_assert_int_eq(bim_node_set_attr(r, "num", "abc"), 0);
    nn(r, "A", "x");
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    nn(r, "A", "x");
    nn(r, "Extra", "x");
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    nn(r, "I", "5");
    nn(r, "A", "x");
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    nn(r, "A", "x");
    nn(r, "I", "notint");
    enc_bad(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    nn(r, NULL, "loose text");
    enc_bad(ty(&s, "ReqT"), r, &eo);

    r = nn(NULL, "Mix", NULL);
    nn(r, NULL, "t1");
    nn(r, NULL, "t2");
    nn(r, "I", "x");
    enc_bad(ty(&s, "MixedType"), r, &eo);
    r = nn(NULL, "Mix", NULL);
    nn(r, "Nope", "x");
    enc_bad(ty(&s, "MixedType"), r, &eo);

    r = nn(NULL, "ChoR", NULL);
    nn(r, "Nope", "x");
    enc_bad(ty(&s, "ChoReq"), r, &eo);
    r = nn(NULL, "ChoR", NULL);
    enc_bad(ty(&s, "ChoReq"), r, &eo);

    r = nn(NULL, "Txt", "notint");
    enc_bad(ty(&s, "TxtT"), r, &eo);

    r = nn(NULL, "AnyO", NULL);
    c = nn(r, "Foo", NULL);
    c->ns = NULL;
    enc_bad(ty(&s, "AnyOther"), r, &eo);
    r = nn(NULL, "AnyL", NULL);
    c = nn(r, "Foo", NULL);
    c->ns = "urn:other";
    enc_bad(ty(&s, "AnyList"), r, &eo);
  }
  sc_free(&s);
}
END_TEST

START_TEST(encode_element_cast_deferred_nil_checks) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_node_t *r;
  bim_node_t *h;
  sc_init(&s, 0);
  eo = eopts(&s);
  r = nn(NULL, "Root", NULL);
  h = nn(r, "Head", NULL);
  nn(h, "A", "x");
  h->cast = ty(&s, "Other");
  eo.cast = 1;
  enc_bad(ty(&s, "RootType"), r, &eo);
  r = nn(NULL, "Root", NULL);
  h = nn(r, "Head", NULL);
  h->deferred = 1;
  enc_bad(ty(&s, "RootType"), r, &eo);
  r = nn(NULL, "Root", NULL);
  h = nn(r, "Head", NULL);
  nn(h, "A", "x");
  ck_assert_int_eq(bim_node_add_attr_ns(h, XSI, "nil", strdup("true")), 0);
  enc_bad(ty(&s, "RootType"), r, &eo);
  eo.deferred = 1;
  r = nn(NULL, "Root", NULL);
  h = nn(r, "Head", NULL);
  h->deferred = 1;
  enc_ok(ty(&s, "RootType"), r, &eo);
  eo.cast = 0;
  eo.deferred = 0;
  r = nn(NULL, "Root", NULL);
  nn(r, "Head", NULL);
  enc_bad(ty(&s, "RootType"), r, &eo);
  sc_free(&s);
}
END_TEST

START_TEST(encode_element_valid_group_shapes) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_schema_t three[3];
  bim_node_t *r;
  bim_node_t *c;
  sc_init(&s, 0);
  eo = eopts(&s);
  memset(three, 0, sizeof three);
  three[0] = s.c->schema;
  three[1] = s.c->schema;
  eo.schemas = three;
  eo.nschemas = 3;
  eo.lax_schema = 2;
  for (int len = 0; len < 3; len++) {
    eo.len_mode = len;
    r = nn(NULL, "Cho", NULL);
    enc_ok(ty(&s, "ChoT"), r, &eo);
    r = nn(NULL, "Cho", NULL);
    nn(r, "B", "b");
    nn(r, "Tail", "t");
    enc_ok(ty(&s, "ChoT"), r, &eo);
    r = nn(NULL, "Cho", NULL);
    nn(r, "Tail", "t");
    enc_ok(ty(&s, "ChoT"), r, &eo);

    r = nn(NULL, "AnyO", NULL);
    c = nn(r, "Foo", NULL);
    c->ns = "urn:foo";
    c = nn(r, "Bar", NULL);
    c->ns = "urn:bar";
    enc_ok(ty(&s, "AnyOther"), r, &eo);
    r = nn(NULL, "AnyL", NULL);
    c = nn(r, "Foo", NULL);
    c->ns = "urn:a";
    c = nn(r, "Head", NULL);
    nn(c, "A", "a");
    enc_ok(ty(&s, "AnyList"), r, &eo);
    r = nn(NULL, "AnyL", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "known", "k"), 0);
    ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "other", strdup("v")), 0);
    ck_assert_int_eq(bim_node_add_attr_ns(r, XSI, "type", strdup("t")), 0);
    enc_ok(ty(&s, "AnyList"), r, &eo);

    r = nn(NULL, "Txt", "42");
    ck_assert_int_eq(bim_node_set_attr(r, "u", "x"), 0);
    enc_ok(ty(&s, "TxtT"), r, &eo);
    r = nn(NULL, "Req", NULL);
    ck_assert_int_eq(bim_node_set_attr(r, "req", "y"), 0);
    ck_assert_int_eq(bim_node_set_attr(r, "num", "7"), 0);
    nn(r, "A", "x");
    nn(r, "I", "5");
    enc_ok(ty(&s, "ReqT"), r, &eo);
    r = nn(NULL, "Mix", NULL);
    enc_ok(ty(&s, "MixedType"), r, &eo);
    r = nn(NULL, "Mix", NULL);
    nn(r, NULL, "only text");
    enc_ok(ty(&s, "MixedType"), r, &eo);
  }
  sc_free(&s);
}
END_TEST

START_TEST(encode_element_all_group_size_limit) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_node_t *r;
  sc_init(&s, 0);
  eo = eopts(&s);
  r = nn(NULL, "BigA", NULL);
  nn(r, "E3", "x");
  enc_bad(ty(&s, "BigAll"), r, &eo);
  r = nn(NULL, "OkA", NULL);
  nn(r, "E63", "x");
  nn(r, "E2", "x");
  enc_ok(ty(&s, "OkAll"), r, &eo);
  r = nn(NULL, "OkA", NULL);
  nn(r, "Nope", "x");
  enc_bad(ty(&s, "OkAll"), r, &eo);
  sc_free(&s);
}
END_TEST

START_TEST(encode_element_fragment_reference_without_href) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_node_t *r;
  sc_init(&s, 0);
  eo = eopts(&s);
  eo.fragref = 1;
  g_res_type = ty(&s, "Base");
  g_res_node = nn(NULL, "Head", NULL);
  nn(g_res_node, "A", "inner");
  r = nn(NULL, "Head", NULL);
  enc_bad(ty(&s, "Base"), r, &eo);
  r = nn(NULL, "Head", NULL);
  r->href = strdup("urn:x");
  enc_ok(ty(&s, "Base"), r, &eo);
  r = nn(NULL, "Head", NULL);
  r->href = strdup("urn:y");
  r->deferred = 1;
  enc_ok(ty(&s, "Base"), r, &eo);
  g_res_fail = 1;
  r = nn(NULL, "Head", NULL);
  r->href = strdup("urn:z");
  enc_ok(ty(&s, "Base"), r, &eo);
  g_res_fail = 0;
  eo.frag_format = 0;
  r = nn(NULL, "Head", NULL);
  enc_ok(ty(&s, "Base"), r, &eo);
  eo.frag_format = 2;
  r = nn(NULL, "Head", NULL);
  r->href = strdup("urn:w");
  enc_ok(ty(&s, "Base"), r, &eo);
  bim_node_free(g_res_node);
  g_res_node = NULL;
  g_res_type = NULL;
  sc_free(&s);
}
END_TEST

static int ext_head(void *ctx, const bim_elem_t *h) {
  (void)ctx;
  return h && h->name && !strcmp(h->name, "Head");
}

START_TEST(encode_element_external_substitution) {
  sc_t s;
  for (int mode = 0; mode < 3; mode++) {
    bim_encode_opts_t eo;
    bim_node_t *r;
    bim_node_t *e;
    sc_init(&s, 1);
    eo = eopts(&s);
    eo.schemas = s.both;
    eo.nschemas = 2;
    eo.ext_subst = ext_head;
    eo.multi = mode == 2 ? 2 : mode;
    eo.cast = 1;
    r = nn(NULL, "Root", NULL);
    e = nn(r, "Ext", NULL);
    nn(e, "V", "v");
    enc_ok(ty(&s, "RootType"), r, &eo);
    r = nn(NULL, "Root", NULL);
    nn(r, "Head", NULL);
    nn(r->kids[0], "A", "a");
    enc_ok(ty(&s, "RootType"), r, &eo);
    r = nn(NULL, "Root", NULL);
    nn(r, "Nowhere", NULL);
    enc_bad(ty(&s, "RootType"), r, &eo);
    r = nn(NULL, "Root", NULL);
    nn(r, "Sub", NULL);
    nn(r->kids[0], "A", "a");
    enc_ok(ty(&s, "RootType"), r, &eo);
    eo.deferred = 1;
    r = nn(NULL, "Root", NULL);
    r->kids = NULL;
    e = nn(r, "Head", NULL);
    e->deferred = 1;
    enc_ok(ty(&s, "RootType"), r, &eo);
    r = nn(NULL, "Root", NULL);
    e = nn(r, "N", NULL);
    ck_assert_int_eq(bim_node_add_attr_ns(e, XSI, "nil", strdup("true")), 0);
    enc_ok(ty(&s, "RootType"), r, &eo);
    eo.freeze = 1;
    r = nn(NULL, "Root", NULL);
    enc_ok(ty(&s, "RootType"), r, &eo);
    eo.freeze = 0;
    eo.deferred = 0;
    r = nn(NULL, "Root", NULL);
    e = nn(r, "Head", NULL);
    nn(e, "A", "a");
    e->cast = xsd_compiled_type(s.c2, NULL, "XT");
    enc_bad(ty(&s, "RootType"), r, &eo);
    eo.compat = BIM_COMPAT_NO_SCHEMA_MODE;
    r = nn(NULL, "Root", NULL);
    enc_ok(ty(&s, "RootType"), r, &eo);
    sc_free(&s);
  }
}
END_TEST

START_TEST(encode_element_type_outside_the_tables) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_node_t *r;
  sc_init(&s, 1);
  eo = eopts(&s);
  eo.schemas = s.both;
  eo.nschemas = 2;
  eo.multi = 2;
  r = nn(NULL, "Inline", NULL);
  nn(r, "Q", "x");
  enc_ok(xsd_compiled_elem(s.c, NULL, "Inline")->type, r, &eo);
  sc_free(&s);
}
END_TEST

static bim_node_t *rich_root(const sc_t *s) {
  bim_node_t *r = nn(NULL, "Root", NULL);
  bim_node_t *h = nn(r, "Head", NULL);
  nn(h, "A", "a");
  ck_assert_int_eq(bim_node_set_attr(h, "k", "kk"), 0);
  h = nn(r, "Sub", NULL);
  nn(h, "A", "b");
  h = nn(r, "Head", NULL);
  nn(h, "A", "c");
  nn(h, "B", "d");
  h->cast = ty(s, "Derived");
  nn(r, "N", "n");
  return r;
}

START_TEST(encode_element_allocation_failures) {
  sc_t s;
  sc_init(&s, 1);
  for (int variant = 0; variant < 4; variant++) {
    for (long n = 0;; n++) {
      bim_encode_opts_t eo = eopts(&s);
      bim_node_t *r = rich_root(&s);
      const bim_type_t *rt = ty(&s, "RootType");
      bitwriter_t bw;
      strrepo_writer_t sw;
      int hit;
      eo.cast = 1;
      eo.len_mode = variant == 1 ? 1 : variant == 2 ? 2 : 0;
      if (variant == 3) {
        eo.schemas = s.both;
        eo.nschemas = 2;
        eo.multi = 2;
        eo.freeze = 1;
      }
      bitwriter_init(&bw);
      ck_assert_int_eq(strrepo_writer_init(&sw), 0);
      alloc_fail_at(n);
      (void)bim_encode_payload_opts(rt, r, &eo, &bw, &sw);
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      bim_node_free(r);
      if (!hit) break;
    }
  }
  sc_free(&s);
}
END_TEST

static bim_node_t *anyl_tree(int variant) {
  bim_node_t *r = nn(NULL, "AnyL", NULL);
  bim_node_t *c;
  ck_assert_int_eq(bim_node_set_attr(r, "ga", "1"), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "other", strdup("v")), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "xa", "2"), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "gb", "3"), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "other2", strdup("w")), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "known", "k"), 0);
  c = nn(r, "Ext", NULL);
  nn(c, "V", "v");
  c = nn(r, "Head", NULL);
  nn(c, "A", "a");
  c = nn(r, "Foo", NULL);
  c->ns = "urn:a";
  if (variant) {
    c = nn(r, "Unknown", NULL);
    nn(c, "Deep", "d");
  }
  return r;
}

START_TEST(encode_any_resolves_attributes_and_elements_across_schemas) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_schema_t three[3];
  bim_node_t *r;
  sc_init(&s, 1);
  for (int lax_known = 0; lax_known < 2; lax_known++)
    for (int len = 0; len < 3; len++) {
      eo = eopts(&s);
      memset(three, 0, sizeof three);
      three[0] = s.c->schema;
      three[1] = s.c2->schema;
      if (lax_known) three[2] = s.c->schema;
      eo.schemas = three;
      eo.nschemas = 3;
      eo.lax_schema = 2;
      eo.len_mode = len;
      eo.multi = 2;
      enc_ok(ty(&s, "AnyList"), anyl_tree(0), &eo);
      enc_ok(ty(&s, "AnyList"), anyl_tree(1), &eo);
      r = nn(NULL, "AnyL", NULL);
      ck_assert_int_eq(bim_node_add_attr_ns(r, "urn:q", "nsd", strdup("v")), 0);
      enc_bad(ty(&s, "AnyList"), r, &eo);
      r = nn(NULL, "AnyL", NULL);
      ck_assert_int_eq(bim_node_set_attr(r, "ga", "1"), 0);
      ck_assert_int_eq(bim_node_add_attr_ns(r, "urn:q", "nsd", strdup("v")), 0);
      enc_bad(ty(&s, "AnyList"), r, &eo);
      r = nn(NULL, "AnyL", NULL);
      ck_assert_int_eq(bim_node_add_attr_ns(r, XSI, "type", strdup("t")), 0);
      enc_ok(ty(&s, "AnyList"), r, &eo);

      eo.lax_schema = -1;
      eo.nschemas = 2;
      eo.schemas = s.both;
      enc_bad(ty(&s, "AnyList"), anyl_tree(0), &eo);
    }
  sc_free(&s);
}
END_TEST

START_TEST(encode_any_survives_allocation_and_writer_failures) {
  sc_t s;
  bim_schema_t three[3];
  sc_init(&s, 1);
  for (int variant = 0; variant < 2; variant++)
    for (int len = 0; len < 3; len++)
      for (long n = 0;; n++) {
        bim_encode_opts_t eo = eopts(&s);
        bim_node_t *r = anyl_tree(variant);
        const bim_type_t *at = ty(&s, "AnyList");
        bitwriter_t bw;
        strrepo_writer_t sw;
        int hit;
        memset(three, 0, sizeof three);
        three[0] = s.c->schema;
        three[1] = s.c2->schema;
        eo.schemas = three;
        eo.nschemas = 3;
        eo.lax_schema = 2;
        eo.len_mode = len;
        eo.multi = 2;
        bitwriter_init(&bw);
        ck_assert_int_eq(strrepo_writer_init(&sw), 0);
        for (int k = 0; k < n % 40; k++) ck_assert_int_eq(bitwriter_put(&bw, 0xA5, 8), 0);
        alloc_fail_at(n / 40);
        (void)bim_encode_payload_opts(at, r, &eo, &bw, &sw);
        hit = alloc_fail_end();
        bitwriter_free(&bw);
        strrepo_writer_free(&sw);
        bim_node_free(r);
        if (!hit && n % 40 == 39) break;
      }
  sc_free(&s);
}
END_TEST

static int deflate_copy(const unsigned char *in, size_t n, unsigned char *out, size_t *outlen) {
  if (n > *outlen) return -1;
  memcpy(out, in, n);
  *outlen = n;
  return 0;
}

static int deflate_never(const unsigned char *, size_t, unsigned char *, size_t *) { return -1; }

static const char *kept(bim_node_t *owner, const char *prefix, int i) {
  char buf[32];
  const char *k;
  snprintf(buf, sizeof buf, "%s%d", prefix, i);
  k = bim_node_keep(owner, buf);
  ck_assert_ptr_nonnull(k);
  return k;
}

static bim_node_t *lax_tree(int nns, int nattrs, int nelems) {
  bim_node_t *top = nn(NULL, "AnyL", NULL);
  bim_node_t *r = nn(top, "Wrap", NULL);
  for (int i = 0; i < nns; i++) {
    bim_node_t *c = nn(r, "E", NULL);
    c->ns = kept(top, "urn:lax:", i);
  }
  if (nattrs) {
    bim_node_t *c = nn(r, "Many", NULL);
    for (int i = 0; i < nattrs; i++) ck_assert_int_eq(bim_node_add_attr_ns(c, NULL, kept(top, "a", i), strdup("v")), 0);
  }
  for (int i = 0; i < nelems; i++) nn(r, kept(top, "e", i), i % 3 ? "t" : NULL);
  return top;
}

static void lax_opts(const sc_t *s, bim_schema_t *two, bim_encode_opts_t *eo, int agg, int zlib, bim_deflate_fn df) {
  memset(two, 0, 2 * sizeof *two);
  two[0] = s->c->schema;
  *eo = eopts(s);
  eo->schemas = two;
  eo->nschemas = 2;
  eo->lax_schema = 1;
  eo->lax_agg = agg;
  eo->lax_zlib = zlib;
  eo->deflate = df;
}

START_TEST(encode_lax_schemes_and_limits) {
  sc_t s;
  bim_schema_t two[2];
  bim_encode_opts_t eo;
  sc_init(&s, 0);
  for (int agg = 0; agg < 2; agg++) {
    lax_opts(&s, two, &eo, agg, 0, NULL);
    enc_ok(ty(&s, "AnyList"), lax_tree(3, 4, 5), &eo);
    lax_opts(&s, two, &eo, agg, 1, deflate_copy);
    enc_ok(ty(&s, "AnyList"), lax_tree(3, 4, 5), &eo);
    lax_opts(&s, two, &eo, agg, 1, NULL);
    enc_bad(ty(&s, "AnyList"), lax_tree(1, 0, 1), &eo);
    lax_opts(&s, two, &eo, agg, 1, deflate_never);
    enc_bad(ty(&s, "AnyList"), lax_tree(1, 0, 1), &eo);
  }
  lax_opts(&s, two, &eo, 0, 0, NULL);
  enc_bad(ty(&s, "AnyList"), lax_tree(300, 0, 0), &eo);
  enc_bad(ty(&s, "AnyList"), lax_tree(0, 16500, 0), &eo);
  enc_bad(ty(&s, "AnyList"), lax_tree(0, 0, 16500), &eo);
  sc_free(&s);
}
END_TEST

START_TEST(encode_lax_survives_allocation_failures) {
  sc_t s;
  bim_schema_t two[2];
  sc_init(&s, 0);
  for (int variant = 0; variant < 4; variant++)
    for (long n = 0;; n++) {
      bim_encode_opts_t eo;
      bim_node_t *r = lax_tree(3, 4, 5);
      const bim_type_t *at = ty(&s, "AnyList");
      bitwriter_t bw;
      strrepo_writer_t sw;
      int hit;
      lax_opts(&s, two, &eo, variant & 1, variant >> 1, deflate_copy);
      bitwriter_init(&bw);
      ck_assert_int_eq(strrepo_writer_init(&sw), 0);
      alloc_fail_at(n);
      (void)bim_encode_payload_opts(at, r, &eo, &bw, &sw);
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      bim_node_free(r);
      if (!hit) break;
    }
  sc_free(&s);
}
END_TEST

static Suite *encode_element_suite(void) {
  Suite *s = suite_create("bim_encode_element");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, encode_element_bad_arguments);
  tcase_add_test(tc, encode_element_rejects_bad_trees);
  tcase_add_test(tc, encode_element_cast_deferred_nil_checks);
  tcase_add_test(tc, encode_element_valid_group_shapes);
  tcase_add_test(tc, encode_element_all_group_size_limit);
  tcase_add_test(tc, encode_element_fragment_reference_without_href);
  tcase_add_test(tc, encode_element_external_substitution);
  tcase_add_test(tc, encode_element_type_outside_the_tables);
  tcase_add_test(tc, encode_element_allocation_failures);
  tcase_add_test(tc, encode_lax_schemes_and_limits);
  tcase_add_test(tc, encode_lax_survives_allocation_failures);
  tcase_add_test(tc, encode_any_resolves_attributes_and_elements_across_schemas);
  tcase_add_test(tc, encode_any_survives_allocation_and_writer_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(encode_element_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
