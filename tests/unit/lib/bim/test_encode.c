/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec.h"
#include "lib/bim/fuu.h"
#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"
#include "../../fx_bim.h"

#define XSI "http://www.w3.org/2001/XMLSchema-instance"

static const char schema_e[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
  "<xs:attribute name=\"ga\" type=\"xs:string\"/>"
  "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\"/></xs:sequence>"
  "<xs:attribute name=\"k\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"Derived\"><xs:complexContent><xs:extension base=\"Base\"><xs:sequence>"
  "<xs:element name=\"B\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
  "<xs:element name=\"Head\" type=\"Base\"/>"
  "<xs:element name=\"Sub\" type=\"Base\" substitutionGroup=\"Head\"/>"
  "<xs:complexType name=\"RootType\"><xs:sequence>"
  "<xs:element ref=\"Head\" minOccurs=\"0\" maxOccurs=\"unbounded\"/>"
  "<xs:element name=\"N\" type=\"xs:string\" nillable=\"true\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Root\" type=\"RootType\"/>"
  "<xs:complexType name=\"MixedType\" mixed=\"true\"><xs:sequence>"
  "<xs:element name=\"I\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"Mix\" type=\"MixedType\"/>"
  "<xs:complexType name=\"AllType\"><xs:all><xs:element name=\"X\" type=\"xs:string\"/>"
  "<xs:element name=\"Y\" type=\"xs:string\" minOccurs=\"0\"/></xs:all></xs:complexType>"
  "<xs:element name=\"AllE\" type=\"AllType\"/>"
  "<xs:complexType name=\"AnyType\"><xs:sequence>"
  "<xs:any namespace=\"##any\" processContents=\"lax\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:anyAttribute processContents=\"lax\"/></xs:complexType>"
  "<xs:element name=\"AnyE\" type=\"AnyType\"/>"
  "<xs:element name=\"Speed\" type=\"xs:float\"/>"
  "<xs:element name=\"Str\" type=\"xs:string\"/>"
  "</xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
} sc_t;

static void sc_init(sc_t *s, int mpegb, const xsd_codec_map_t *codecs, int ncodecs) {
  xsd_compile_opts_t o = {mpegb, 0, 0, codecs, ncodecs, 0};
  xml_doc_t *doc = NULL;
  s->set = xsd_set_new();
  ck_assert_ptr_nonnull(s->set);
  ck_assert_int_eq(xml_doc_parse(schema_e, strlen(schema_e), &doc, NULL), 0);
  if (xsd_set_add_doc(s->set, doc, "e.xsd") || xsd_set_resolve(s->set) || xsd_compile(s->set, &o, &s->c)) ck_abort_msg("%s %s", xsd_set_error_code(s->set), xsd_set_error(s->set));
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

static bim_node_t *base(bim_node_t *parent, const char *name, const char *k, const char *a) {
  bim_node_t *n = nn(parent, name, NULL);
  if (k) ck_assert_int_eq(bim_node_set_attr(n, "k", k), 0);
  nn(n, "A", a);
  return n;
}

static int same(const char *a, const char *b) { return a == b || (a && b && !strcmp(a, b)) || ((!a || !*a) && (!b || !*b)); }

static int tree_eq(const bim_node_t *a, const bim_node_t *b) {
  if (!same(a->name, b->name) || !same(a->ns, b->ns) || !same(a->text, b->text) || a->nattrs != b->nattrs || a->nkids != b->nkids || a->cast != b->cast || !!a->deferred != !!b->deferred ||
      !same(a->href, b->href))
    return 0;
  for (int i = 0; i < a->nattrs; i++) {
    const char *v = bim_node_attr_ns(b, a->attrs[i].ns, a->attrs[i].name);
    if (!v || strcmp(v, a->attrs[i].value)) return 0;
  }
  for (int i = 0; i < a->nkids; i++)
    if (!tree_eq(a->kids[i], b->kids[i])) return 0;
  return 1;
}

typedef struct {
  const bim_schema_t *schemas;
  int nschemas;
  bim_aod_t *aod;
  const char *const *prefix;
  bim_fragref_fn resolve;
} dopts_t;

static void roundtrip(const bim_encode_opts_t *eo, const dopts_t *d, const bim_type_t *t, const char *rootname, const bim_node_t *tree) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  bim_decode_opts_t o;
  bim_node_t *got = NULL;
  const unsigned char *b;
  const unsigned char *s;
  size_t bl;
  size_t sl;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(t, tree, eo, &bw, &sw), 0);
  b = bitwriter_data(&bw, &bl);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, b, bl);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  memset(&o, 0, sizeof o);
  o.schemas = d->schemas;
  o.nschemas = d->nschemas;
  o.mpegb = eo->mpegb;
  o.lax_schema = eo->lax_schema;
  o.inflate = ident;
  o.aod = d->aod;
  o.schema_prefix = d->prefix;
  o.resolve = d->resolve;
  o.frag_formats = (const unsigned char *)"\x01";
  o.nfrag_formats = 1;
  ck_assert_int_eq(bim_decode_payload(t, rootname, &o, &br, eo->mpegb ? NULL : &sr, &got), 0);
  ck_assert_msg(tree_eq(tree, got), "decoded tree differs");
  bim_node_free(got);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}

static bim_encode_opts_t eopts(const sc_t *s, int mpegb) {
  bim_encode_opts_t eo;
  memset(&eo, 0, sizeof eo);
  eo.schemas = &s->c->schema;
  eo.nschemas = 1;
  eo.mpegb = mpegb;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  return eo;
}

static bim_node_t *feature_tree(const sc_t *s, int deferred) {
  bim_node_t *r = nn(NULL, "Root", NULL);
  bim_node_t *d;
  base(r, "Head", "1", "a");
  base(r, "Sub", NULL, "b");
  d = base(r, "Head", NULL, "c");
  nn(d, "B", "d");
  d->cast = ty(s, "Derived");
  if (deferred) {
    d = nn(r, "Head", NULL);
    d->deferred = 1;
  }
  d = nn(r, "N", NULL);
  ck_assert_int_eq(bim_node_add_attr_ns(d, XSI, "nil", strdup("true")), 0);
  return r;
}

/* ISO 23001-1 7.3.2, 7.4.2, 7.4.5: lengths, substitution, type cast, nil, deferred */
START_TEST(encode_mono_options) {
  sc_t s;
  sc_init(&s, 1, NULL, 0);
  for (int len = 0; len < 3; len++)
    for (int def = 0; def < 2; def++) {
      bim_encode_opts_t eo = eopts(&s, 1);
      dopts_t d = {&s.c->schema, 1, NULL, NULL, NULL};
      bim_node_t *t = feature_tree(&s, def);
      eo.len_mode = len;
      eo.deferred = def;
      eo.cast = 1;
      roundtrip(&eo, &d, ty(&s, "RootType"), "Root", t);
      bim_node_free(t);
    }
  xsd_set_free(s.set);
}
END_TEST

/* 7.5.2.4.4 mixed content, 7.5.2.2.5.2.5 all groups */
START_TEST(encode_mixed_and_all) {
  sc_t s;
  bim_encode_opts_t eo;
  dopts_t d;
  bim_node_t *m;
  bim_node_t *a;
  sc_init(&s, 1, NULL, 0);
  eo = eopts(&s, 1);
  d = (dopts_t){&s.c->schema, 1, NULL, NULL, NULL};
  m = nn(NULL, "Mix", NULL);
  nn(m, NULL, "t1");
  nn(m, "I", "x");
  nn(m, NULL, "t2");
  nn(m, "I", "y");
  nn(m, NULL, "t3");
  roundtrip(&eo, &d, ty(&s, "MixedType"), "Mix", m);
  bim_node_free(m);
  a = nn(NULL, "AllE", NULL);
  nn(a, "Y", "1");
  nn(a, "X", "2");
  roundtrip(&eo, &d, ty(&s, "AllType"), "AllE", a);
  bim_node_free(a);
  a = nn(NULL, "AllE", NULL);
  nn(a, "X", "2");
  roundtrip(&eo, &d, ty(&s, "AllType"), "AllE", a);
  m = nn(NULL, "AllE", NULL);
  nn(m, "X", "3");
  ck_assert(!tree_eq(a, m));
  bim_node_free(m);
  bim_node_free(a);
  xsd_set_free(s.set);
}
END_TEST

/* 7.4.3, 7.4.4: cast defined by another schema in a second chunk */
START_TEST(encode_multi_version) {
  sc_t s;
  bim_schema_t two[2];
  dopts_t d;
  for (int multi = 1; multi < 3; multi++) {
    bim_encode_opts_t eo;
    bim_node_t *r = nn(NULL, "Root", NULL);
    bim_node_t *h;
    sc_init(&s, 1, NULL, 0);
    two[0] = s.c->schema;
    two[0].subs = NULL;
    two[0].nsubs = 0;
    two[1] = s.c->schema;
    eo = eopts(&s, 1);
    eo.schemas = two;
    eo.nschemas = 2;
    eo.cast = 1;
    eo.multi = multi;
    d = (dopts_t){two, 2, NULL, NULL, NULL};
    base(r, "Head", NULL, "a");
    h = base(r, "Head", "k", "c");
    nn(h, "B", "d");
    h->cast = ty(&s, "Derived");
    roundtrip(&eo, &d, ty(&s, "RootType"), "Root", r);
    eo.freeze = 1;
    bim_node_free(h->kids[1]);
    h->nkids = 1;
    h->cast = NULL;
    roundtrip(&eo, &d, ty(&s, "RootType"), "Root", r);
    bim_node_free(r);
    xsd_set_free(s.set);
  }
}
END_TEST

/* 7.5.2.4.5.2, 7.5.3.2, 7.5.3.3, Amd 2 lax */
START_TEST(encode_wildcards_and_lax) {
  sc_t s;
  bim_schema_t two[3];
  dopts_t d;
  sc_init(&s, 1, NULL, 0);
  memset(two, 0, sizeof two);
  two[0] = s.c->schema;
  two[1] = s.c->schema;
  for (int zl = 0; zl < 2; zl++)
    for (int agg = 0; agg < 2; agg++) {
      bim_encode_opts_t eo = eopts(&s, 1);
      bim_node_t *r = nn(NULL, "AnyE", NULL);
      bim_node_t *f;
      eo.schemas = two;
      eo.nschemas = 3;
      eo.lax_schema = 2;
      eo.lax_zlib = zl;
      eo.lax_agg = agg;
      eo.deflate = ident;
      d = (dopts_t){two, 3, NULL, NULL, NULL};
      ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "ga", strdup("g")), 0);
      ck_assert_int_eq(bim_node_add_attr_ns(r, NULL, "zz", strdup("lax")), 0);
      base(r, "Head", "1", "a");
      f = nn(r, "Foo", NULL);
      f->ns = "urn:foo";
      ck_assert_int_eq(bim_node_add_attr_ns(f, NULL, "q", strdup("v")), 0);
      nn(f, NULL, "text");
      nn(f, "Bar", NULL)->ns = "urn:foo";
      roundtrip(&eo, &d, ty(&s, "AnyType"), "AnyE", r);
      bim_node_free(r);
    }
  xsd_set_free(s.set);
}
END_TEST

/* ISO 23001-1 8.5 .. 8.8 */
START_TEST(encode_advanced_decoders) {
  sc_t s;
  bim_encode_opts_t eo;
  dopts_t d;
  bim_aod_inst_t insts[3];
  bim_aod_map_t maps[2];
  bim_aod_t aod;
  static const int i0[] = {0};
  static const int i1[] = {1, 2};
  static const char *const prefix[] = {"p"};
  bim_node_t *n;
  sc_init(&s, 1, NULL, 0);
  memset(insts, 0, sizeof insts);
  insts[0] = (bim_aod_inst_t){.kind = BIM_AOD_UQ, .vmin = 0, .vmax = 10, .nbits = 4};
  insts[1].kind = BIM_AOD_QNAME;
  insts[2].kind = BIM_AOD_ZLIB;
  maps[0] = (bim_aod_map_t){xsd_compiled_elem(s.c, NULL, "Speed")->type->simple, 0, i0, 1, NULL};
  maps[1] = (bim_aod_map_t){xsd_compiled_elem(s.c, NULL, "Str")->type->simple, 0, i1, 1, NULL};
  aod = (bim_aod_t){insts, 3, maps, 2};
  eo = eopts(&s, 1);
  eo.aod = &aod;
  eo.schema_prefix = prefix;
  eo.deflate = ident;
  d = (dopts_t){&s.c->schema, 1, &aod, prefix, NULL};
  n = nn(NULL, "Speed", "2");
  roundtrip(&eo, &d, xsd_compiled_elem(s.c, NULL, "Speed")->type, "Speed", n);
  bim_node_free(n);
  n = nn(NULL, "Str", "p:name");
  roundtrip(&eo, &d, xsd_compiled_elem(s.c, NULL, "Str")->type, "Str", n);
  bim_node_free(n);
  maps[1].inst = i1 + 1;
  n = nn(NULL, "Str", "zipped");
  roundtrip(&eo, &d, xsd_compiled_elem(s.c, NULL, "Str")->type, "Str", n);
  bim_node_free(n);
  n = nn(NULL, "Speed", "11");
  {
    bitwriter_t bw;
    strrepo_writer_t sw;
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    ck_assert_int_eq(bim_encode_payload_opts(xsd_compiled_elem(s.c, NULL, "Speed")->type, n, &eo, &bw, &sw), -1);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
  bim_node_free(n);
  xsd_set_free(s.set);
}
END_TEST

static int resolve(void *ctx, const char *href, unsigned format, unsigned char **out, size_t *outlen) {
  const bitwriter_t *frag = ctx;
  (void)href;
  if (format != 1) return -1;
  *out = malloc(frag->len);
  if (!*out) return -1;
  memcpy(*out, frag->buf, frag->len);
  *outlen = frag->len;
  return 0;
}

static void frag_decode(const sc_t *s, const bim_encode_opts_t *eo, const bim_node_t *h, const bim_decode_opts_t *o, bim_node_t **got) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  const unsigned char *b;
  size_t bl;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(ty(s, "Base"), h, eo, &bw, &sw), 0);
  b = bitwriter_data(&bw, &bl);
  bitreader_init(&br, b, bl);
  ck_assert_int_eq(bim_decode_payload(ty(s, "Base"), "Head", o, &br, NULL, got), 0);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}

/* 7.4.8, 7.4.9 */
START_TEST(encode_fragment_references) {
  sc_t s;
  bim_encode_opts_t eo;
  bim_decode_opts_t o;
  bim_node_t *h;
  bim_node_t *got = NULL;
  bitwriter_t frag;
  strrepo_writer_t sw;
  sc_init(&s, 1, NULL, 0);
  eo = eopts(&s, 1);
  h = base(NULL, "Head", "k", "inner");
  bitwriter_init(&frag);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_encode_payload_opts(ty(&s, "Base"), h, &eo, &frag, &sw), 0);
  bitwriter_flush(&frag);
  strrepo_writer_free(&sw);
  bim_node_free(h);
  eo.fragref = 1;
  h = nn(NULL, "Head", NULL);
  h->href = strdup("urn:frag");
  ck_assert_ptr_nonnull(h->href);
  memset(&o, 0, sizeof o);
  o.schemas = &s.c->schema;
  o.nschemas = 1;
  o.mpegb = 1;
  o.lax_schema = -1;
  o.resolve = resolve;
  o.ctx = &frag;
  o.frag_formats = (const unsigned char *)"\x01";
  o.nfrag_formats = 1;
  h->deferred = 1;
  frag_decode(&s, &eo, h, &o, &got);
  ck_assert_int_eq(got->deferred, 1);
  ck_assert_str_eq(got->href, "urn:frag");
  bim_node_free(got);
  h->deferred = 0;
  frag_decode(&s, &eo, h, &o, &got);
  ck_assert_int_eq(got->deferred, 0);
  ck_assert_str_eq(got->kids[0]->text, "inner");
  ck_assert_str_eq(got->href, "urn:frag");
  bim_node_free(got);
  bim_node_free(h);
  bitwriter_free(&frag);
  xsd_set_free(s.set);
}
END_TEST

/* ISO 15938-1:2002 and TS 102 323 table 58: string offset after a length coded element */
START_TEST(encode_string_offset_after_length) {
  sc_t s;
  bim_encode_opts_t eo;
  dopts_t d;
  bim_node_t *r;
  static const xsd_codec_map_t codecs[] = {{XSD_NS, "string", BIM_STRING, 1}};
  sc_init(&s, 0, codecs, 1);
  eo = eopts(&s, 0);
  eo.len_mode = 2;
  d = (dopts_t){&s.c->schema, 1, NULL, NULL, NULL};
  r = nn(NULL, "Root", NULL);
  base(r, "Head", "kk", "one");
  base(r, "Head", NULL, "two");
  nn(r, "N", "three");
  roundtrip(&eo, &d, ty(&s, "RootType"), "Root", r);
  bim_node_free(r);
  xsd_set_free(s.set);
}
END_TEST

typedef struct {
  sc_t sc;
  bim_decoderinit_t di;
  bim_fuu_state_t st;
  strrepo_writer_t sw;
  bim_encode_opts_t eo;
} fx_t;

static void fx_init(fx_t *f, int insert) {
  memset(f, 0, sizeof *f);
  sc_init(&f->sc, 1, NULL, 0);
  f->di.mpegb = 1;
  f->di.nschemas = 1;
  f->di.insert = insert;
  f->st.di = &f->di;
  f->st.schemas = &f->sc.c->schema;
  f->st.nschemas = 1;
  f->st.opts.lax_schema = -1;
  f->eo = eopts(&f->sc, 1);
  ck_assert_int_eq(strrepo_writer_init(&f->sw), 0);
}

static void fx_free(fx_t *f) {
  bim_fuu_state_close(&f->st);
  strrepo_writer_free(&f->sw);
  xsd_set_free(f->sc.set);
}

static bim_tbc_t tbc(bim_tbc_kind_t kind, const char *name, const bim_type_t *type) {
  bim_tbc_t t;
  memset(&t, 0, sizeof t);
  t.kind = kind;
  t.name = name;
  t.type = type;
  return t;
}

/* Root, context end, operand */
static bim_tbc_t *path(const fx_t *f, bim_tbc_kind_t kind, const char *name, const bim_type_t *type, const bim_simple_t *simple) {
  bim_tbc_t *t = calloc(3, sizeof *t);
  ck_assert_ptr_nonnull(t);
  t[0] = tbc(BIM_TBC_ELEM, "Root", ty(&f->sc, "RootType"));
  t[1] = tbc(BIM_TBC_END, NULL, ty(&f->sc, "RootType"));
  t[2] = tbc(kind, name, type);
  t[2].simple = simple;
  return t;
}

static int fwrite_bits(fx_t *f, const bim_fuu_t *u, bitwriter_t *bw, bim_err_t *err) {
  bitwriter_init(bw);
  return bim_fuu_write(&f->st, u, &f->eo, &f->sw, bw, err);
}

static void fparse(fx_t *f, bitwriter_t *bw, bim_fuu_t *g) {
  bim_bits_t b = {NULL, 0, NULL};
  size_t n;
  bim_err_t err;
  b.bits = bitwriter_data(bw, &n);
  b.nbits = (uint64_t)bw->len * 8 + (uint64_t)bw->cur_bits;
  b.nbits = b.nbits ? b.nbits : (uint64_t)n * 8;
  if (bim_fuu_parse(&f->st, &b, g, &err)) ck_abort_msg("%s %s", err.code, err.msg);
}

static void ftbc_eq(const bim_tbc_t *a, const bim_tbc_t *b) {
  ck_assert_int_eq(a->kind, b->kind);
  ck_assert_int_eq(same(a->name, b->name), 1);
  ck_assert_ptr_eq(a->type, b->type);
}

/* written FUU parses back to the same command, path, positions and payloads */
static void fuu_check(fx_t *f, const bim_fuu_t *u) {
  bitwriter_t bw;
  bim_fuu_t g;
  bim_err_t err;
  if (fwrite_bits(f, u, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  fparse(f, &bw, &g);
  ck_assert_uint_eq(g.command, u->command);
  ck_assert_uint_eq(g.mode, u->mode);
  ck_assert_uint_eq(g.ntbcs, u->ntbcs);
  for (size_t i = 0; i < u->ntbcs; i++) ftbc_eq(&g.tbcs[i], &u->tbcs[i]);
  ck_assert_uint_eq(g.npayloads, u->npayloads);
  for (size_t j = 0; j < g.npayloads; j++) {
    size_t i = 0;
    while (i < u->npayloads && memcmp(&g.pos[j * g.ntbcs], &u->pos[i * u->ntbcs], g.ntbcs * sizeof *g.pos)) i++;
    ck_assert_uint_lt(i, u->npayloads);
    if (u->payloads) ck_assert(tree_eq(u->payloads[i], g.payloads[j]));
    if (u->values) ck_assert_str_eq(u->values[i], g.values[j]);
  }
  bim_fuu_free(&g);
  bitwriter_free(&bw);
}

static bim_fuu_t fuu(unsigned cmd, unsigned mode, bim_tbc_t *tbcs, size_t ntbcs, size_t npay) {
  bim_fuu_t u;
  memset(&u, 0, sizeof u);
  u.command = cmd;
  u.mode = mode;
  u.tbcs = tbcs;
  u.ntbcs = ntbcs;
  u.npayloads = npay;
  u.pos = calloc(npay * ntbcs, sizeof *u.pos);
  ck_assert_ptr_nonnull(u.pos);
  return u;
}

static void fuu_release(bim_fuu_t *u) {
  for (size_t i = 0; u->payloads && i < u->npayloads; i++) bim_node_free(u->payloads[i]);
  for (size_t i = 0; u->values && i < u->npayloads; i++) free(u->values[i]);
  free(u->payloads);
  free(u->values);
  free(u->tbcs);
  free(u->pos);
}

static void put_nodes(bim_fuu_t *u, size_t n, const char *name, const char *k, const char *a) {
  u->payloads = calloc(n, sizeof *u->payloads);
  ck_assert_ptr_nonnull(u->payloads);
  for (size_t i = 0; i < n; i++) {
    char txt[32];
    snprintf(txt, sizeof txt, "%s%zu", a, i);
    u->payloads[i] = base(NULL, name, k, txt);
  }
}

/* hand-computed bits, ISO 23001-1 6.4.2, 6.6.5.2: AddContent, ContextModeCode 001, selector Root of 8 global
   elements (4 bits), ContextTBC end 11, OperandTBC Head 01, no substitution 0, no cast 0, SPC vluimsbf5 */
START_TEST(fuu_writer_matches_hand_coded_bits) {
  fx_t f;
  bim_fuu_t u;
  bitwriter_t bw;
  bitwriter_t exp;
  bim_err_t err;
  const unsigned char *a;
  const unsigned char *b;
  size_t al;
  size_t bl;
  fx_init(&f, 0);
  u = fuu(BIM_CMD_ADD, 1, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"), NULL), 3, 1);
  u.pos[2] = 3;
  put_nodes(&u, 1, "Head", "k", "a");
  if (fwrite_bits(&f, &u, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  bitwriter_init(&exp);
  ck_assert_int_eq(bitwriter_put(&exp, 1, 4) || bitwriter_put(&exp, 1, 3) || bitwriter_put(&exp, 4, 4) || bitwriter_put(&exp, 3, 2) || bitwriter_put(&exp, 1, 2) ||
                       bitwriter_put(&exp, 0, 2) || bitwriter_put_vluimsbf5(&exp, 3),
                   0);
  ck_assert_int_eq(bim_encode_payload_opts(ty(&f.sc, "Base"), u.payloads[0], &f.eo, &exp, &f.sw), 0);
  a = bitwriter_data(&bw, &al);
  b = bitwriter_data(&exp, &bl);
  ck_assert_uint_eq(al, bl);
  ck_assert_mem_eq(a, b, al);
  bitwriter_free(&bw);
  bitwriter_free(&exp);
  fuu_release(&u);
  fx_free(&f);
}
END_TEST

/* 6.5, 6.6.5.5, 6.6.5.6: commands, substitution, multiple payloads in integer and rational positions */
START_TEST(fuu_writer_round_trips) {
  for (int insert = 0; insert < 2; insert++) {
    fx_t f;
    bim_fuu_t u;
    fx_init(&f, insert);
    u = fuu(BIM_CMD_ADD, 1, path(&f, BIM_TBC_ELEM, "Sub", ty(&f.sc, "Base"), NULL), 3, 1);
    u.pos[2] = insert ? 1ULL << 63 : 2;
    put_nodes(&u, 1, "Sub", NULL, "s");
    fuu_check(&f, &u);
    fuu_release(&u);
    u = fuu(BIM_CMD_ADD, 3, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Derived"), NULL), 3, 3);
    u.pos[2] = insert ? 1ULL << 62 : 5;
    u.pos[5] = insert ? 1ULL << 63 : 1;
    u.pos[8] = insert ? 3ULL << 62 : 9;
    put_nodes(&u, 3, "Head", "k", "d");
    for (int i = 0; i < 3; i++) nn(u.payloads[i], "B", "b");
    fuu_check(&f, &u);
    fuu_release(&u);
    {
      bim_tbc_t *t = calloc(4, sizeof *t);
      ck_assert_ptr_nonnull(t);
      t[0] = tbc(BIM_TBC_ELEM, "Root", ty(&f.sc, "RootType"));
      t[1] = tbc(BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"));
      t[2] = tbc(BIM_TBC_END, NULL, ty(&f.sc, "Base"));
      t[3] = tbc(BIM_TBC_ATTR, "k", ty(&f.sc, "Base"));
      t[3].simple = ty(&f.sc, "Base")->attrs[0].simple;
      u = fuu(BIM_CMD_REPLACE, 1, t, 4, 1);
      u.pos[1] = insert ? 1ULL << 63 : 1;
      u.values = calloc(1, sizeof *u.values);
      ck_assert_ptr_nonnull(u.values);
      u.values[0] = strdup("v");
      fuu_check(&f, &u);
      fuu_release(&u);
    }
    u = fuu(BIM_CMD_DELETE, 1, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"), NULL), 3, 1);
    u.pos[2] = insert ? 1ULL << 63 : 0;
    fuu_check(&f, &u);
    fuu_release(&u);
    {
      bim_fuu_t r = fuu(BIM_CMD_RESET, 0, NULL, 0, 0);
      fuu_check(&f, &r);
      free(r.pos);
    }
    fx_free(&f);
  }
}
END_TEST

static void fuu_apply_written(fx_t *f, const bim_fuu_t *u) {
  bitwriter_t bw;
  bim_fuu_t g;
  bim_err_t err;
  if (fwrite_bits(f, u, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  fparse(f, &bw, &g);
  if (bim_fuu_apply(&f->st, &g, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  bim_fuu_free(&g);
  bitwriter_free(&bw);
}

static bim_tbc_t *tbcs_of(size_t n, const bim_tbc_t *src) {
  bim_tbc_t *t = calloc(n, sizeof *t);
  ck_assert_ptr_nonnull(t);
  memcpy(t, src, n * sizeof *t);
  return t;
}

/* 6.6.5.2: topmost operand, relative path, character data, user data, wildcard attribute */
START_TEST(fuu_writer_path_forms) {
  fx_t f;
  bim_fuu_t u;
  bim_tbc_t top[2];
  bim_tbc_t rel[3];
  bim_tbc_t chr[3];
  bim_tbc_t any[3];
  bim_node_t *r;
  fx_init(&f, 0);
  top[0] = tbc(BIM_TBC_END, NULL, NULL);
  top[1] = tbc(BIM_TBC_ELEM, "Root", ty(&f.sc, "RootType"));
  u = fuu(BIM_CMD_ADD, 1, tbcs_of(2, top), 2, 1);
  u.payloads = calloc(1, sizeof *u.payloads);
  ck_assert_ptr_nonnull(u.payloads);
  r = nn(NULL, "Root", NULL);
  base(r, "Head", "1", "x");
  u.payloads[0] = r;
  fuu_check(&f, &u);
  fuu_apply_written(&f, &u);
  fuu_release(&u);

  u = fuu(BIM_CMD_ADD, 1, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"), NULL), 3, 1);
  u.pos[2] = 1;
  put_nodes(&u, 1, "Head", NULL, "y");
  fuu_apply_written(&f, &u);
  fuu_release(&u);
  rel[0] = tbc(BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"));
  rel[1] = tbc(BIM_TBC_END, NULL, ty(&f.sc, "Base"));
  rel[2] = tbc(BIM_TBC_ATTR, "k", ty(&f.sc, "Base"));
  rel[2].simple = ty(&f.sc, "Base")->attrs[0].simple;
  for (unsigned mode = 2; mode < 5; mode += 2) {
    u = fuu(BIM_CMD_ADD, mode, tbcs_of(3, rel), 3, 1);
    u.pos[0] = 1;
    u.values = calloc(1, sizeof *u.values);
    ck_assert_ptr_nonnull(u.values);
    u.values[0] = strdup("rel");
    fuu_check(&f, &u);
    fuu_release(&u);
  }

  chr[0] = tbc(BIM_TBC_ELEM, "Mix", ty(&f.sc, "MixedType"));
  chr[1] = tbc(BIM_TBC_END, NULL, ty(&f.sc, "MixedType"));
  chr[2] = tbc(BIM_TBC_CHARDATA, NULL, ty(&f.sc, "MixedType"));
  chr[2].simple = &bim_xs_string;
  u = fuu(BIM_CMD_REPLACE, 1, tbcs_of(3, chr), 3, 1);
  u.pos[2] = 2;
  u.values = calloc(1, sizeof *u.values);
  ck_assert_ptr_nonnull(u.values);
  u.values[0] = strdup("text");
  fuu_check(&f, &u);
  fuu_release(&u);

  u = fuu(BIM_CMD_ADD, 1, tbcs_of(2, top), 2, 1);
  u.tbcs[0] = tbc(BIM_TBC_ELEM, "Root", ty(&f.sc, "RootType"));
  u.tbcs[1] = tbc(BIM_TBC_END, NULL, ty(&f.sc, "RootType"));
  u.user_data = 1;
  {
    bitwriter_t bw;
    bim_fuu_t g;
    bim_err_t err;
    ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), 0);
    fparse(&f, &bw, &g);
    ck_assert_int_eq(g.user_data, 1);
    bim_fuu_free(&g);
    bitwriter_free(&bw);
  }
  fuu_release(&u);

  any[0] = tbc(BIM_TBC_ELEM, "AnyE", ty(&f.sc, "AnyType"));
  any[1] = tbc(BIM_TBC_END, NULL, ty(&f.sc, "AnyType"));
  any[2] = tbc(BIM_TBC_ATTR, "ga", ty(&f.sc, "AnyType"));
  any[2].simple = f.st.schemas->attrs[0].simple;
  u = fuu(BIM_CMD_ADD, 1, tbcs_of(3, any), 3, 1);
  u.values = calloc(1, sizeof *u.values);
  ck_assert_ptr_nonnull(u.values);
  u.values[0] = strdup("g");
  fuu_check(&f, &u);
  fuu_release(&u);
  fx_free(&f);
}
END_TEST

/* 6.2.1 ContextPathTable, 6.4.2 reparameterization, error codes */
START_TEST(fuu_writer_table_reparam_errors) {
  fx_t f;
  bim_fuu_t u;
  bitwriter_t bw;
  bitwriter_t pb;
  bim_err_t err;
  bim_di_cpath_t cp;
  bim_fuu_t g;
  fx_init(&f, 0);
  bitwriter_init(&pb);
  ck_assert_int_eq(bitwriter_put(&pb, 1, 3) || bitwriter_put(&pb, 4, 4) || bitwriter_put(&pb, 3, 2) || bitwriter_put(&pb, 1, 2) || bitwriter_put(&pb, 0, 2), 0);
  memset(&cp, 0, sizeof cp);
  cp.nbits = 13;
  {
    size_t n;
    cp.bits = bitwriter_data(&pb, &n);
  }
  f.di.cpt = 1;
  f.di.cpt_code_len = 1;
  f.di.cpaths = &cp;
  f.di.ncpaths = 1;
  u = fuu(BIM_CMD_ADD, 5, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"), NULL), 3, 1);
  u.pos[2] = 4;
  put_nodes(&u, 1, "Head", NULL, "t");
  fuu_check(&f, &u);
  f.di.cpt = 0;
  ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  fuu_release(&u);
  bitwriter_free(&pb);

  f.di.aod = 1;
  u = fuu(BIM_CMD_RESET, 0, NULL, 0, 0);
  for (int v = 1; v < 3; v++) {
    u.reparam = v;
    ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), 0);
    fparse(&f, &bw, &g);
    ck_assert_int_eq(g.reparam, v);
    bim_fuu_free(&g);
    bitwriter_free(&bw);
  }
  u.reparam = 0;
  ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  f.di.aod = 0;
  u.command = 9;
  ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  free(u.pos);
  u = fuu(BIM_CMD_ADD, 1, path(&f, BIM_TBC_ELEM, "Nope", ty(&f.sc, "Base"), NULL), 3, 1);
  ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400304");
  bitwriter_free(&bw);
  fuu_release(&u);
  u = fuu(BIM_CMD_ADD, 2, path(&f, BIM_TBC_ELEM, "Head", ty(&f.sc, "Base"), NULL), 3, 1);
  ck_assert_int_eq(fwrite_bits(&f, &u, &bw, &err), -1);
  ck_assert_str_eq(err.code, "LE400306");
  bitwriter_free(&bw);
  fuu_release(&u);
  fx_free(&f);
}
END_TEST

static Suite *encode_suite(void) {
  Suite *s = suite_create("bim_encode");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, encode_mono_options);
  tcase_add_test(tc, encode_mixed_and_all);
  tcase_add_test(tc, encode_multi_version);
  tcase_add_test(tc, encode_wildcards_and_lax);
  tcase_add_test(tc, encode_advanced_decoders);
  tcase_add_test(tc, encode_fragment_references);
  tcase_add_test(tc, encode_string_offset_after_length);
  tcase_add_test(tc, fuu_writer_matches_hand_coded_bits);
  tcase_add_test(tc, fuu_writer_round_trips);
  tcase_add_test(tc, fuu_writer_path_forms);
  tcase_add_test(tc, fuu_writer_table_reparam_errors);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(encode_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
