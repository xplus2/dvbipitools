/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/bim/codec.h"
#include "lib/bim/dvbstream.h"
#include "lib/bim/dvbtva.h"
#include "lib/bim/sdnsbim.h"
#include "lib/bim/tvabim.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/xml_util.h"
#include "../../fx_bim.h"

static void datetime_bytes(const char *v, const unsigned char *want) {
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  char out[64];
  bitreader_t br;
  bitwriter_init(&bw);
  ck_assert_int_eq(tva_datetime_encode(&bw, v), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(n, 8);
  ck_assert_mem_eq(p, want, 8);
  bitreader_init(&br, p, n);
  ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), 0);
  bitwriter_free(&bw);
}

/* TS 102 822-3-2 4.4.2.4.2.1 examples */
START_TEST(tvadatetime_matches_spec_examples) {
  static const unsigned char a[] = {0x00, 0x00, 0xAD, 0x2A, 0x00, 0x6D, 0xDD, 0x00};
  static const unsigned char b[] = {0x00, 0x00, 0xC9, 0x75, 0x02, 0x9C, 0x59, 0xA8};
  static const unsigned char c[] = {0x00, 0x00, 0xCB, 0x01, 0x00, 0x00, 0x00, 0x00};
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *p;
  size_t n;
  char out[64];
  datetime_bytes("1980-04-01T02:00:00Z", a);
  datetime_bytes("2000-01-30T12:10:01Z", b);
  datetime_bytes("2001-03-01T00:00:00Z", c);
  datetime_bytes("2000-01-30T14:10:01+02:00", b);
  datetime_bytes("2000-01-30T12:10:01", b);
  datetime_bytes("2000-01-29T23:10:01-13:00", b);
  bitwriter_init(&bw);
  ck_assert_int_eq(tva_datetime_encode(&bw, "2000-01-30T12:10:01.5Z"), 0);
  p = bitwriter_data(&bw, &n);
  bitreader_init(&br, p, n);
  ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), 0);
  ck_assert_str_eq(out, "2000-01-30T12:10:01.5Z");
  bitwriter_free(&bw);
}
END_TEST

START_TEST(tvadatetime_rejects_unrepresentable) {
  bitwriter_t bw;
  bitreader_t br;
  unsigned char bad[] = {0, 0, 0, 0, 0x05, 0x26, 0x5C, 0x00};
  char out[64];
  bitwriter_init(&bw);
  ck_assert_int_eq(tva_datetime_encode(&bw, "1858-11-16T23:00:00Z"), -1);
  ck_assert_int_eq(tva_datetime_encode(&bw, "2000-01-30T12:10:01.0015Z"), -1);
  ck_assert_int_eq(tva_datetime_encode(&bw, "2000-01-30"), -1);
  ck_assert_int_eq(tva_datetime_encode(&bw, "garbage"), -1);
  ck_assert_int_eq(tva_datetime_encode(&bw, "2000-01-30T12:10:01.0000Z"), 0);
  bitwriter_free(&bw);
  bitreader_init(&br, bad, sizeof bad);
  ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), -1);
  bitreader_init(&br, bad, 4);
  ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), -1);
}
END_TEST

/* 4.4.2.4.3.1: MJD in 4 bytes */
START_TEST(tvadate_matches_spec_example) {
  static const unsigned char want[] = {0x00, 0x00, 0xAD, 0x2A};
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *p;
  size_t n;
  char out[32];
  bitwriter_init(&bw);
  ck_assert_int_eq(tva_date_encode(&bw, "1980-04-01"), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(n, 4);
  ck_assert_mem_eq(p, want, 4);
  bitreader_init(&br, p, n);
  ck_assert_int_eq(tva_date_decode(&br, out, sizeof out), 0);
  ck_assert_str_eq(out, "1980-04-01");
  ck_assert_int_eq(tva_date_decode(&br, out, sizeof out), -1);
  ck_assert_int_eq(tva_date_encode(&bw, "1980-02-30"), -1);
  ck_assert_int_eq(tva_date_encode(&bw, "1980-04-01Z"), -1);
  ck_assert_int_eq(tva_date_encode(&bw, "1980-4-1"), -1);
  bitwriter_free(&bw);
}
END_TEST

#define TVA "urn:tva:metadata:2004"

static const char schema_tva[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" TVA "\" targetNamespace=\"" TVA "\" elementFormDefault=\"qualified\">"
  "<xs:complexType name=\"ProgramInformationType\"><xs:sequence><xs:element name=\"Title\" type=\"xs:string\" minOccurs=\"0\"/>"
  "<xs:element name=\"Created\" type=\"xs:dateTime\" minOccurs=\"0\"/><xs:element name=\"Day\" type=\"xs:date\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"programId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"ProgramInformationTableType\"><xs:sequence><xs:element name=\"ProgramInformation\" type=\"ProgramInformationType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ProgramDescriptionType\"><xs:sequence><xs:element name=\"ProgramInformationTable\" type=\"ProgramInformationTableType\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"TVAMainType\"><xs:sequence><xs:element name=\"ProgramDescription\" type=\"ProgramDescriptionType\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"publisher\" type=\"xs:string\"/></xs:complexType>"
  "<xs:element name=\"TVAMain\" type=\"TVAMainType\"/></xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  bim_tvans_t ns;
  bim_decoderinit_t di;
  bim_fuu_state_t st;
  bim_encode_opts_t eo;
  bim_decode_opts_t dop;
} tb_t;

static void tb_init(tb_t *t, size_t zsize, int gather) {
  xsd_codec_map_t codecs[BIM_TVABIM_CODECS];
  xsd_compile_opts_t o = {0, 1, 0, codecs, 0, 0};
  xml_doc_t *doc = NULL;
  memset(t, 0, sizeof *t);
  t->ns.tva = TVA;
  o.ncodecs = bim_tvabim_codecs(codecs);
  t->set = xsd_set_new();
  ck_assert_ptr_nonnull(t->set);
  ck_assert_int_eq(xml_doc_parse(schema_tva, strlen(schema_tva), &doc, NULL), 0);
  if (xsd_set_add_doc(t->set, doc, "t.xsd") || xsd_set_resolve(t->set) || xsd_compile(t->set, &o, &t->c)) ck_abort_msg("%s %s", xsd_set_error_code(t->set), xsd_set_error(t->set));
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, NULL, 0, &t->di), 0);
  t->dop.inflate = ident;
  t->dop.zbuf_size = zsize;
  bim_tvabim_state(t->c, &t->di, &t->dop, &t->st);
  t->eo.lax_schema = -1;
  t->eo.frag_format = 1;
  t->eo.deflate = ident;
  t->eo.zbuf_size = zsize;
  t->eo.zgather = gather;
}

static void tb_free(tb_t *t) {
  bim_tvainit_decoderinit_free(&t->di);
  xsd_set_free(t->set);
}

static bim_node_t *tn(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, text) : bim_node_new(name);
  ck_assert_ptr_nonnull(n);
  n->ns = TVA;
  if (!parent && text) ck_assert_int_eq(bim_node_set_text(n, text), 0);
  return n;
}

static bim_node_t *program(const char *id, const char *title) {
  bim_node_t *p = tn(NULL, "ProgramInformation", NULL);
  ck_assert_int_eq(bim_node_add_attr_ns(p, NULL, "programId", strdup(id)), 0);
  tn(p, "Title", title);
  tn(p, "Created", "2024-05-01T10:00:00Z");
  tn(p, "Day", "2024-05-01");
  return p;
}

/* TS 102 822-3-2 4.4.2: ReplaceContent, absolute path to the parent, position code, payload. Hand coded:
   command 0010, ContextModeCode 001, selector 0 (one global element), ContextTBC ProgramDescription 01 and
   ProgramInformationTable 01, end 11, operand 1, SPC vluimsbf5 */
START_TEST(tvabim_fuu_bits_match_hand_coding) {
  tb_t t;
  bim_tvabim_fuu_t f = {0x0001, BIM_CMD_REPLACE, 3, NULL};
  bitwriter_t bw;
  bitwriter_t exp;
  bitwriter_t pay;
  strrepo_writer_t sw;
  bim_err_t err;
  const unsigned char *a;
  const unsigned char *b;
  size_t al;
  size_t bl;
  tb_init(&t, 0, 0);
  f.root = program("crid://x/1", "T");
  bitwriter_init(&bw);
  bitwriter_init(&exp);
  bitwriter_init(&pay);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, &f, 1, &bw, NULL, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_int_eq(bitwriter_put(&pay, 2, 4) || bitwriter_put(&pay, 1, 3) || bitwriter_put(&pay, 0, 1) || bitwriter_put(&pay, 1, 2) || bitwriter_put(&pay, 1, 2) || bitwriter_put(&pay, 3, 2) ||
                       bitwriter_put(&pay, 1, 1) || bitwriter_put_vluimsbf5(&pay, 3),
                   0);
  t.eo.schemas = &t.c->schema;
  t.eo.nschemas = 1;
  ck_assert_int_eq(bim_encode_payload_opts(xsd_compiled_type(t.c, TVA, "ProgramInformationType"), f.root, &t.eo, &pay, &sw), 0);
  b = bitwriter_data(&pay, &bl);
  ck_assert_int_eq(bitwriter_put_vluimsbf8(&exp, 1) || bitwriter_put_vluimsbf8(&exp, bl) || bitwriter_put_bytes(&exp, b, bl), 0);
  a = bitwriter_data(&bw, &al);
  b = bitwriter_data(&exp, &bl);
  ck_assert_uint_eq(al, bl);
  ck_assert_mem_eq(a, b, al);
  bim_node_free(f.root);
  bitwriter_free(&bw);
  bitwriter_free(&exp);
  bitwriter_free(&pay);
  strrepo_writer_free(&sw);
  tb_free(&t);
}
END_TEST

static void round_trip(size_t zsize, int gather) {
  tb_t t;
  bim_tvabim_fuu_t in[4];
  bim_tvabim_fuu_t *got;
  bim_fragref_t refs[4];
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *b;
  size_t bl;
  size_t n;
  tb_init(&t, zsize, gather);
  in[0] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_REPLACE, 0, program("crid://example.com/a", "A long programme title spanning chunks")};
  in[1] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_REPLACE, 7, program("crid://example.com/b", "Short")};
  in[2] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_DELETE, 7, NULL};
  in[3] = (bim_tvabim_fuu_t){0x000E, BIM_CMD_REPLACE, 0, tn(NULL, "TVAMain", NULL)};
  ck_assert_int_eq(bim_node_add_attr_ns(in[3].root, NULL, "publisher", strdup("tvlistings.tv/TV%20Listings")), 0);
  bitwriter_init(&bw);
  if (bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 4, &bw, refs, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  b = bitwriter_data(&bw, &bl);
  ck_assert_uint_eq(b[0], 4);
  ck_assert_uint_eq(refs[0].au_off, 1);
  for (int i = 1; i < 4; i++) ck_assert_uint_gt(refs[i].au_off, refs[i - 1].au_off);
  if (bim_tvabim_au_parse(t.c, &t.ns, &t.st, b, bl, &got, &n, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_uint_eq(n, 4);
  for (size_t i = 0; i < 4; i++) {
    ck_assert_uint_eq(got[i].code, in[i].code);
    ck_assert_uint_eq(got[i].command, in[i].command);
    if (in[i].code == 0x0001) ck_assert_uint_eq(got[i].position, in[i].position);
    ck_assert_int_eq(!!got[i].root, !!in[i].root);
    if (in[i].root) ck_assert_msg(tree_same(in[i].root, got[i].root), "fragment %zu differs", i);
  }
  bim_tvabim_au_free(got, n);
  for (int i = 0; i < 4; i++) bim_node_free(in[i].root);
  bitwriter_free(&bw);
  tb_free(&t);
}

/* Zlib strings with BufferSize, one chunk per string or gathered, tvadateTime and tvadate values, delete without payload */
START_TEST(tvabim_access_unit_round_trips) {
  round_trip(0, 0);
  round_trip(0, 1);
  round_trip(8, 0);
  round_trip(8, 1);
  round_trip(1, 1);
  round_trip(100000, 1);
}
END_TEST

/* fragments leave their container chain behind, the main fragment keeps the tables down to the fragment roots */
START_TEST(tvafrag_split_and_join_round_trip) {
  tb_t t;
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *tbl = tn(tn(doc, "ProgramDescription", NULL), "ProgramInformationTable", NULL);
  bim_node_t *joined = NULL;
  bim_tvapart_t *parts = NULL;
  size_t n = 0;
  tb_init(&t, 0, 0);
  ck_assert_int_eq(bim_node_add_attr_ns(doc, NULL, "publisher", strdup("tvlistings.tv/TV%20Listings")), 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/b", "B")), 0);
  ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n), 0);
  ck_assert_uint_eq(n, 3);
  ck_assert_uint_eq(parts[0].code, 0x000E);
  ck_assert_int_eq(parts[0].root->nattrs, 1);
  ck_assert_int_eq(parts[0].root->nkids, 1);
  ck_assert_str_eq(parts[0].root->kids[0]->name, "ProgramDescription");
  ck_assert_int_eq(parts[0].root->kids[0]->kids[0]->nkids, 0);
  ck_assert_uint_eq(parts[1].code, 0x0001);
  ck_assert_str_eq(bim_node_attr(parts[2].root, "programId"), "crid://example.com/b");
  ck_assert_int_eq(bim_tvafrag_join(t.c, &t.ns, parts, n, &joined), 0);
  ck_assert_msg(tree_same(doc, joined), "joined document differs");
  ck_assert_ptr_null(parts[1].root);
  bim_tvafrag_free(parts, n);
  bim_node_free(joined);
  parts = NULL;
  ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc->kids[0], &parts, &n), -2);
  bim_node_free(doc);
  tb_free(&t);
}
END_TEST

START_TEST(tvafrag_names_roots_steps_and_codes) {
  tb_t t;
  bim_tvaroot_t steps[8];
  bim_tvaroot_t root;
  ck_assert_str_eq(bim_tvafrag_name(0x0001), "ProgramInformation");
  ck_assert_str_eq(bim_tvafrag_name(0x000E), "TVAMain");
  ck_assert_str_eq(bim_tvafrag_name(0x0011), "MetadataOriginationInformation");
  ck_assert_str_eq(bim_tvafrag_name(0x0012), "Package");
  ck_assert_str_eq(bim_tvafrag_name(0x0017), "Rule");
  ck_assert_str_eq(bim_tvafrag_name(0x0022), "PushDownloadProgram");
  ck_assert_ptr_null(bim_tvafrag_name(0x0000));
  ck_assert_ptr_null(bim_tvafrag_name(0x7777));
  tb_init(&t, 0, 0);
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x0001, steps, 8), 4);
  ck_assert_str_eq(steps[0].name, "TVAMain");
  ck_assert_str_eq(steps[1].name, "ProgramDescription");
  ck_assert_str_eq(steps[2].name, "ProgramInformationTable");
  ck_assert_str_eq(steps[3].name, "ProgramInformation");
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x0001, steps, 3), -1);
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x000E, steps, 8), 1);
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x0005, steps, 8), -1);
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x0012, steps, 8), -1);
  ck_assert_int_eq(bim_tvafrag_steps(t.c, &t.ns, 0x7777, steps, 8), -1);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0001, &root), 0);
  ck_assert_str_eq(root.name, "ProgramInformation");
  ck_assert_str_eq(root.ns, TVA);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0005, &root), -1);
  ck_assert_int_eq(bim_tvafrag_code(t.c, &t.ns, "ProgramInformation", TVA, steps[3].type), 0x0001);
  ck_assert_int_eq(bim_tvafrag_code(t.c, &t.ns, "ProgramInformation", TVA, steps[0].type), -1);
  ck_assert_int_eq(bim_tvafrag_code(t.c, &t.ns, "Nope", TVA, steps[3].type), -1);
  tb_free(&t);
}
END_TEST

START_TEST(tvans_detect_finds_extended_and_interstitial_namespaces) {
  static const char ext[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:tva:metadata:extended:2010\"><xs:element name=\"X\" type=\"xs:string\"/></xs:schema>";
  static const char itg[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:tva:metadata:int:2010\"><xs:element name=\"Y\" type=\"xs:string\"/></xs:schema>";
  xsd_compile_opts_t o = {0};
  xsd_set_t *set = xsd_set_new();
  const xsd_compiled_t *c;
  xml_doc_t *doc = NULL;
  bim_tvans_t ns = {TVA, "stale", "stale"};
  ck_assert_ptr_nonnull(set);
  ck_assert_int_eq(xml_doc_parse(ext, strlen(ext), &doc, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(set, doc, "e.xsd"), 0);
  ck_assert_int_eq(xml_doc_parse(itg, strlen(itg), &doc, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(set, doc, "i.xsd"), 0);
  ck_assert_int_eq(xsd_set_resolve(set), 0);
  ck_assert_int_eq(xsd_compile(set, &o, &c), 0);
  bim_tvans_detect(c, &ns);
  ck_assert_str_eq(ns.tva, TVA);
  ck_assert_str_eq(ns.tva2, "urn:tva:metadata:extended:2010");
  ck_assert_str_eq(ns.intr, "urn:tva:metadata:int:2010");
  xsd_set_free(set);
  {
    tb_t t;
    tb_init(&t, 0, 0);
    ns.intr = "stale";
    ns.tva2 = ns.intr;
    bim_tvans_detect(t.c, &ns);
    ck_assert_ptr_null(ns.tva2);
    ck_assert_ptr_null(ns.intr);
    tb_free(&t);
  }
}
END_TEST

START_TEST(tvafrag_copy_is_deep) {
  tb_t t;
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *tbl = tn(tn(doc, "ProgramDescription", NULL), "ProgramInformationTable", NULL);
  bim_tvapart_t *parts = NULL;
  bim_tvapart_t *copy = NULL;
  size_t n = 0;
  tb_init(&t, 0, 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n), 0);
  ck_assert_uint_eq(n, 2);
  ck_assert_int_eq(bim_tvafrag_copy(parts, n, &copy), 0);
  for (size_t i = 0; i < n; i++) {
    ck_assert_uint_eq(copy[i].code, parts[i].code);
    ck_assert_ptr_ne(copy[i].root, parts[i].root);
    ck_assert_msg(tree_same(copy[i].root, parts[i].root), "copy %zu differs", i);
  }
  ck_assert_int_eq(bim_node_set_attr(copy[1].root, "programId", "crid://example.com/changed"), 0);
  ck_assert_str_eq(bim_node_attr(parts[1].root, "programId"), "crid://example.com/a");
  bim_tvafrag_free(copy, n);
  bim_tvafrag_free(parts, n);
  bim_node_free(doc);
  tb_free(&t);
}
END_TEST

/* TS 102 323 9.4.4: DecoderInit lists more schemas, reader knowing only the first skips their chunks */
START_TEST(dvbstream_head_lists_every_schema_and_several_schemas_round_trip) {
  static const char *const more[] = {"urn:test:ext"};
  tb_t t;
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *tbl = tn(tn(doc, "ProgramDescription", NULL), "ProgramInformationTable", NULL);
  bim_tvapart_t *parts = NULL;
  dvbtva_fuu_t *back = NULL;
  dvbtva_schemas_t sch;
  dvbtva_roots_t r;
  dvbstream_head_t head;
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_decoderinit_t di;
  bim_encode_opts_t eo;
  bim_decode_opts_t dop;
  bitwriter_t hw;
  bitwriter_t au;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *h;
  const unsigned char *a;
  const unsigned char *s;
  size_t hl;
  size_t al;
  size_t sl;
  size_t n = 0;
  size_t nb = 0;
  int same;
  bim_err_t err;
  tb_init(&t, 0, 0);
  r = (dvbtva_roots_t){t.c, &t.ns};
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(dvbtva_schemas_init(&sch, t.c, 2, NULL), 0);
  ck_assert_int_eq(sch.n, 2);
  ck_assert_int_eq(sch.v[0].known, 1);
  ck_assert_int_eq(sch.v[1].known, 0);
  ck_assert_int_eq(dvbtva_schemas_init(&sch, t.c, 0, NULL), -1);
  ck_assert_int_eq(dvbtva_schemas_init(&sch, t.c, DVBTVA_SCHEMAS + 1, NULL), -1);
  ck_assert_int_eq(dvbtva_schemas_init(&sch, t.c, 2, NULL), 0);
  ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n), 0);
  eo = t.eo;
  eo.schemas = sch.v;
  eo.nschemas = sch.n;
  dop = t.dop;
  dop.schemas = sch.v;
  dop.nschemas = sch.n;
  bitwriter_init(&hw);
  bitwriter_init(&au);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, more, 1, &di), 0);
  ck_assert_int_eq(bim_tvainit_write(&ti, &hw, &err), 0);
  ck_assert_int_eq(bim_decoderinit_write(&di, NULL, NULL, &hw, &err), 0);
  ck_assert_int_eq(dvbtva_au_write(dvbtva_tva_root, &r, &eo, parts, n, &au, &sw, NULL, &err), 0);
  h = bitwriter_data(&hw, &hl);
  a = bitwriter_data(&au, &al);
  s = strrepo_writer_data(&sw, &sl);
  ck_assert_int_eq(dvbstream_head(h, hl, &head, &err), DVBSTREAM_OK);
  ck_assert_uint_eq(head.nschemas, 2);
  ck_assert_str_eq(head.uri[0], TVA);
  ck_assert_str_eq(head.uri[1], "urn:test:ext");
  ck_assert_uint_eq(head.used, hl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(dvbtva_au_parse(dvbtva_tva_root, &r, &dop, &br, &sr, NULL, 0, &back, &nb, &err), 0);
  ck_assert_uint_eq(nb, n);
  for (size_t i = 0; i < n; i++) ck_assert_msg(tree_same(parts[i].root, back[i].root), "fragment %zu differs", i);
  /* reader without second schema misreads mode bits */
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  dop.nschemas = 1;
  dvbtva_au_free(back, nb);
  back = NULL;
  same = dvbtva_au_parse(dvbtva_tva_root, &r, &dop, &br, &sr, NULL, 0, &back, &nb, &err) == 0 && nb == n && tree_same(parts[0].root, back[0].root);
  ck_assert_int_eq(same, 0);
  dvbtva_au_free(back, back ? nb : 0);
  bim_tvainit_decoderinit_free(&di);
  strrepo_writer_free(&sw);
  bitwriter_free(&au);
  bitwriter_free(&hw);
  bim_tvafrag_free(parts, n);
  bim_node_free(doc);
  tb_free(&t);
}
END_TEST

/* TVAMain document to DVB-TVA stream and back, with the failures a reader reports */
START_TEST(dvbstream_round_trips_a_document) {
  tb_t t;
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *tbl = tn(tn(doc, "ProgramDescription", NULL), "ProgramInformationTable", NULL);
  bim_node_t *back = NULL;
  dvbtva_roots_t r;
  unsigned char *buf = NULL;
  size_t len = 0;
  dvbstream_head_t head;
  size_t counts[DVBSTREAM_CODES] = {0};
  size_t sizes[2];
  bim_err_t err;
  tb_init(&t, 0, 0);
  r = (dvbtva_roots_t){t.c, &t.ns};
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/b", "B")), 0);
  ck_assert_int_eq(dvbstream_write(&r, &t.eo, doc, 0, &buf, &len, counts, sizes, &err), DVBSTREAM_OK);
  ck_assert_uint_eq(counts[0x0001], 2);
  ck_assert_uint_eq(counts[0x000E], 1);
  ck_assert_int_eq(dvbstream_head(buf, len, &head, &err), DVBSTREAM_OK);
  ck_assert_uint_eq(head.nschemas, 1);
  ck_assert_str_eq(head.uri[0], TVA);
  ck_assert_int_eq(dvbstream_read(&r, &t.dop, buf + head.used, len - head.used, &back, NULL, &err), DVBSTREAM_OK);
  ck_assert_msg(tree_same(doc, back), "document differs");
  ck_assert_int_eq(dvbstream_read(&r, &t.dop, buf + head.used, 3, &back, NULL, &err), DVBSTREAM_SHORT);
  ck_assert_int_eq(dvbstream_read(&r, &t.dop, buf + head.used, 6, &back, NULL, &err), DVBSTREAM_LENGTH);
  ck_assert_int_eq(dvbstream_head(buf, 2, &head, &err), DVBSTREAM_INIT);
  ck_assert_int_eq(dvbstream_write(&r, &t.eo, doc->kids[0], 0, &buf, &len, NULL, sizes, &err), DVBSTREAM_ROOT);
  ck_assert_int_eq(bim_node_add_attr_ns(doc, NULL, "publisher", strdup("My Guide")), 0);
  ck_assert_int_eq(dvbstream_write(&r, &t.eo, doc, 1, &buf, &len, NULL, sizes, &err), DVBSTREAM_RULE);
  ck_assert_ptr_nonnull(strstr(err.msg, "8.9"));
  bim_node_free(back);
  bim_node_free(doc);
  free(buf);
  tb_free(&t);
}
END_TEST

START_TEST(tvabim_gathering_shares_chunks) {
  size_t size[2];
  for (int g = 0; g < 2; g++) {
    tb_t t;
    bim_tvabim_fuu_t f = {0x0001, BIM_CMD_REPLACE, 0, NULL};
    bitwriter_t bw;
    bim_err_t err;
    size_t bl;
    tb_init(&t, 64, g);
    f.root = program("crid://example.com/a", "Title");
    bitwriter_init(&bw);
    if (bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, &f, 1, &bw, NULL, &err)) ck_abort_msg("%s %s", err.code, err.msg);
    (void)bitwriter_data(&bw, &bl);
    size[g] = bl;
    bim_node_free(f.root);
    bitwriter_free(&bw);
    tb_free(&t);
  }
  ck_assert_uint_lt(size[1], size[0]);
}
END_TEST

/* TS 102 822-3-2 4.6.1.1: container with fragment_reference_format 0x00 and no string repository */
START_TEST(tvabim_container_carries_access_unit) {
  tb_t t;
  bim_tvabim_fuu_t in[2];
  bim_fragref_t refs[2];
  bim_fragref_t *got = NULL;
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *b;
  const unsigned char *au;
  const unsigned char *sr;
  size_t bl;
  size_t aul;
  size_t srl;
  size_t cl;
  unsigned char *cont = NULL;
  unsigned fmt;
  int n;
  tb_init(&t, 0, 0);
  in[0] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_REPLACE, 1, program("crid://example.com/a", "A")};
  in[1] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_REPLACE, 2, program("crid://example.com/b", "B")};
  bitwriter_init(&bw);
  if (bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 2, &bw, refs, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  b = bitwriter_data(&bw, &bl);
  for (unsigned i = 0; i < 2; i++) {
    refs[i].frag_id = 10 + i;
    refs[i].version = (uint8_t)(1 + i);
  }
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_BIM, b, bl, NULL, 0, refs, 2, &cont, &cl), 0);
  ck_assert_int_eq(bim_container_parse(cont, cl, &au, &aul, &sr, &srl), 0);
  ck_assert_ptr_null(sr);
  ck_assert_uint_eq(aul, bl);
  ck_assert_int_eq(bim_container_parse_refs(cont, cl, &fmt, &got, &n), 0);
  ck_assert_uint_eq(fmt, BIM_FRAGREF_BIM);
  ck_assert_int_eq(n, 2);
  for (int i = 0; i < 2; i++) {
    ck_assert_uint_eq(got[i].au_off, refs[i].au_off);
    ck_assert_uint_eq(got[i].frag_id, refs[i].frag_id);
    ck_assert_uint_eq(got[i].version, refs[i].version);
    ck_assert_uint_eq(got[i].str_off, 0);
  }
  ck_assert_int_eq(bim_container_build(0xE1 + 1, b, bl, NULL, 0, refs, 2, &cont, &cl), -1);
  free(got);
  free(cont);
  bim_node_free(in[0].root);
  bim_node_free(in[1].root);
  bitwriter_free(&bw);
  tb_free(&t);
}
END_TEST

START_TEST(tvabim_rejects_invalid_fragments) {
  tb_t t;
  bim_tvabim_fuu_t f = {0x0001, BIM_CMD_REPLACE, 0, NULL};
  bitwriter_t bw;
  bim_err_t err;
  tb_init(&t, 0, 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, &f, 1, &bw, NULL, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVABIM_ERR);
  f.code = 0x0099;
  f.root = program("crid://x/1", "T");
  bitwriter_free(&bw);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, &f, 1, &bw, NULL, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVABIM_ERR);
  f.code = 0x0001;
  f.command = 9;
  ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, &f, 1, &bw, NULL, &err), -1);
  bim_node_free(f.root);
  bitwriter_free(&bw);
  tb_free(&t);
}
END_TEST

#define SDNS "urn:dvb:metadata:iptv:sdns:2008-1"

static const char schema_sdns[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" SDNS "\" targetNamespace=\"" SDNS "\" elementFormDefault=\"qualified\">"
  "<xs:complexType name=\"ServiceType\"><xs:sequence><xs:element name=\"Name\" type=\"xs:string\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"id\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"ServiceDiscoveryType\"><xs:sequence><xs:element name=\"Service\" type=\"ServiceType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"version\" type=\"xs:unsignedByte\"/></xs:complexType>"
  "<xs:element name=\"ServiceDiscovery\" type=\"ServiceDiscoveryType\"/></xs:schema>";

/* TS 102 034 5.5.2: DecoderInit with the SD&S schema and the Zlib codec signalled, one FUU, DVBContextPath 0x0030 */
START_TEST(sdnsbim_segment_round_trips) {
  xsd_compile_opts_t o = {0, 1, 0, NULL, 0, 0};
  xsd_set_t *set = xsd_set_new();
  const xsd_compiled_t *c;
  xml_doc_t *doc = NULL;
  bim_sdnsroots_t roots;
  bim_decoderinit_t di;
  bim_decoderinit_t g;
  bitwriter_t bw;
  bim_err_t err;
  bim_encode_opts_t eo;
  bim_decode_opts_t dopts;
  strrepo_writer_t sw;
  bitreader_t br;
  dvbtva_fuu_t in;
  dvbtva_fuu_t *got;
  const unsigned char *p;
  size_t n;
  size_t used;
  bim_node_t *sd;
  bim_node_t *sv;
  uint64_t strcode = 0;
  ck_assert_ptr_nonnull(set);
  ck_assert_int_eq(xml_doc_parse(schema_sdns, strlen(schema_sdns), &doc, NULL), 0);
  if (xsd_set_add_doc(set, doc, "s.xsd") || xsd_set_resolve(set) || xsd_compile(set, &o, &c)) ck_abort_msg("%s %s", xsd_set_error_code(set), xsd_set_error(set));
  roots = (bim_sdnsroots_t){c, SDNS};
  ck_assert_int_eq(bim_sdnsbim_decoderinit(c, SDNS, &di), 0);
  ck_assert_uint_eq(di.schemas[0].ncodecs, 1);
  ck_assert_str_eq(di.schemas[0].codecs[0].uri, BIM_SDNS_ZLIB_CODEC);
  for (int i = 1; i < c->schema.ntypes; i++)
    if (c->schema.types[i]->name && !strcmp(c->schema.types[i]->name, "string") && !strcmp(c->schema.types[i]->ns, XSD_NS)) strcode = (uint64_t)i - 1;
  {
    int found = 0;
    for (size_t i = 0; i < di.schemas[0].codecs[0].ntypes; i++) found = found || di.schemas[0].codecs[0].types[i] == strcode;
    ck_assert_int_eq(found, 1);
  }
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&di, NULL, NULL, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(p[0], 0);
  ck_assert_uint_eq(p[1], 0x1F);
  ck_assert_uint_eq(p[2], 1);
  ck_assert_uint_eq(p[3], strlen(SDNS));
  ck_assert_uint_eq(p[4 + strlen(SDNS)], 0);
  ck_assert_uint_eq(p[5 + strlen(SDNS)], 1);
  ck_assert_uint_eq(p[6 + strlen(SDNS)], strlen(BIM_SDNS_ZLIB_CODEC));
  ck_assert_mem_eq(p + 7 + strlen(SDNS), BIM_SDNS_ZLIB_CODEC, strlen(BIM_SDNS_ZLIB_CODEC));
  ck_assert_uint_eq(p[n - 1], 0);
  ck_assert_int_eq(bim_decoderinit_parse(p, n, 0, NULL, NULL, &g, &used, &err), 0);
  ck_assert_str_eq(g.schemas[0].uri, SDNS);
  ck_assert_str_eq(g.schemas[0].codecs[0].uri, BIM_SDNS_ZLIB_CODEC);
  bim_decoderinit_free(&g);
  bitwriter_free(&bw);

  sd = tn(NULL, "ServiceDiscovery", NULL);
  sd->ns = SDNS;
  ck_assert_int_eq(bim_node_add_attr_ns(sd, NULL, "version", strdup("3")), 0);
  sv = bim_node_add_new(sd, "Service", NULL);
  sv->ns = SDNS;
  ck_assert_int_eq(bim_node_add_attr_ns(sv, NULL, "id", strdup("svc-1")), 0);
  bim_node_add_new(sv, "Name", "First name")->ns = SDNS;
  bim_node_add_new(sv, "Name", "Second name")->ns = SDNS;
  in = (dvbtva_fuu_t){BIM_SDNS_CONTEXT_PATH, sd};
  memset(&eo, 0, sizeof eo);
  eo.schemas = &c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  eo.deflate = ident;
  eo.zgather = 1;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (dvbtva_au_write(bim_sdnsbim_root, &roots, &eo, &in, 1, &bw, &sw, NULL, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(p[0], 1);
  ck_assert_uint_eq(p[2], 0x00);
  ck_assert_uint_eq(p[3], 0x30);
  memset(&dopts, 0, sizeof dopts);
  dopts.schemas = &c->schema;
  dopts.nschemas = 1;
  dopts.lax_schema = -1;
  dopts.inflate = ident;
  bitreader_init(&br, p, n);
  if (dvbtva_au_parse(bim_sdnsbim_root, &roots, &dopts, &br, NULL, NULL, 0, &got, &used, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_uint_eq(used, 1);
  ck_assert_msg(tree_same(sd, got[0].root), "segment differs");
  dvbtva_au_free(got, used);
  in.code = 0x0031;
  bitwriter_free(&bw);
  bitwriter_init(&bw);
  ck_assert_int_eq(dvbtva_au_write(bim_sdnsbim_root, &roots, &eo, &in, 1, &bw, &sw, NULL, &err), -1);
  bim_node_free(sd);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bim_sdnsbim_decoderinit_free(&di);
  xsd_set_free(set);
}
END_TEST

/* 23001-1 segment via schema dir, ns from file or given, loader errors */
START_TEST(sdnsdoc_decodes_a_segment_through_a_loaded_schema) {
  char dir[] = "/tmp/dvbipitools_sdnsdoc_XXXXXX";
  char path[300];
  const char *paths[1] = {dir};
  const char *culprit = NULL;
  bim_sdnsdoc_t d;
  bim_node_t *sd;
  bim_node_t *sv;
  dvbtva_fuu_t in;
  bim_sdnsroots_t roots;
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_err_t err;
  const unsigned char *p;
  size_t n;
  char *xml = NULL;
  size_t xl = 0;
  FILE *f;
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(path, sizeof path, "%s/sdns.xsd", dir);
  f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(schema_sdns, 1, strlen(schema_sdns), f), strlen(schema_sdns));
  fclose(f);
  ck_assert_int_eq(bim_sdnsdoc_open(&d, paths, 1, NULL, &culprit), -3);
  bim_sdnsdoc_close(&d);
  ck_assert_int_eq(bim_sdnsdoc_open(&d, paths, 1, SDNS, &culprit), 0);
  ck_assert_str_eq(d.ns, SDNS);
  d.inflate = ident;
  sd = bim_node_new("ServiceDiscovery");
  sd->ns = SDNS;
  sv = bim_node_add_new(sd, "Service", NULL);
  sv->ns = SDNS;
  ck_assert_int_eq(bim_node_set_attr(sv, "id", "svc1"), 0);
  bim_node_add_new(sv, "Name", "Only name")->ns = SDNS;
  in = (dvbtva_fuu_t){BIM_SDNS_CONTEXT_PATH, sd};
  roots = (bim_sdnsroots_t){d.c, SDNS};
  memset(&eo, 0, sizeof eo);
  eo.schemas = &d.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  eo.mpegb = 1;
  eo.deflate = ident;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (dvbtva_au_write(bim_sdnsbim_root, &roots, &eo, &in, 1, &bw, &sw, NULL, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  ck_assert_int_eq(bim_sdnsdoc_to_xml(&d, p, n, &xml, &xl), 0);
  ck_assert_ptr_nonnull(strstr(xml, "<ServiceDiscovery xmlns=\"" SDNS "\""));
  ck_assert_ptr_nonnull(strstr(xml, "<Name>Only name</Name>"));
  free(xml);
  ck_assert_int_eq(bim_sdnsdoc_to_xml(&d, p, n / 2, &xml, &xl), -1);
  {
    const char *plain;
    char *owned;
    ck_assert_int_eq(bim_sdnsdoc_segment_xml(&d, 0, (const unsigned char *)"<a/>", 4, &plain, &xl, &owned), 0);
    ck_assert_uint_eq(xl, 4);
    ck_assert_int_eq(bim_sdnsdoc_segment_xml(&d, 2, p, n, &plain, &xl, &owned), -2);
    ck_assert_int_eq(bim_sdnsdoc_segment_xml(&d, 1, p, n, &plain, &xl, &owned), 0);
    ck_assert_ptr_eq(plain, owned);
    free(owned);
  }
  bim_node_free(sd);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bim_sdnsdoc_close(&d);
  ck_assert_int_eq(bim_sdnsdoc_open(&d, (const char *[]){"/tmp/dvbipitools_sdnsdoc_missing"}, 1, SDNS, &culprit), XSD_LOAD_STAT);
  bim_sdnsdoc_close(&d);
  unlink(path);
  rmdir(dir);
}
END_TEST

static void report_into(char *out, size_t cap, int writing, int rc, const unsigned char *p, size_t len, const size_t sizes[2], const bim_err_t *err) {
  FILE *f = tmpfile();
  int saved = dup(2);
  size_t n;
  if (!f) abort();
  if (saved < 0) abort();
  fflush(stderr);
  ck_assert_int_ge(dup2(fileno(f), 2), 0);
  dvbstream_report("BE", writing, rc, p, len, sizes, err);
  fflush(stderr);
  ck_assert_int_ge(dup2(saved, 2), 0);
  close(saved);
  ck_assert_int_eq(fseek(f, 0, SEEK_SET), 0);
  n = fread(out, 1, cap - 1, f);
  ck_assert_int_eq(ferror(f), 0);
  out[n] = '\0';
  fclose(f);
}

START_TEST(dvbstream_report_names_every_failure) {
  static const struct {
    int writing;
    int rc;
    const char *code;
  } cases[] = {
    {0, DVBSTREAM_INIT, "BE400242"}, {0, DVBSTREAM_SHORT, "BE400103"}, {0, DVBSTREAM_LENGTH, "BE400104"}, {1, DVBSTREAM_LENGTH, "BE400108"},
    {0, DVBSTREAM_REPO, "BE400105"}, {1, DVBSTREAM_OOM, "BE500101"},     {1, DVBSTREAM_RULE, "BE400318"},   {1, DVBSTREAM_AU, "BE400315"},
    {0, DVBSTREAM_AU, "BE400316"},
  };
  unsigned char in[8] = {0, 0, 0, 100, 1, 2, 3, 4};
  size_t sizes[2] = {7, 9};
  bim_err_t err;
  char out[512];
  memset(&err, 0, sizeof err);
  snprintf(err.msg, sizeof err.msg, "reason text");
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    report_into(out, sizeof out, cases[i].writing, cases[i].rc, in, sizeof in, sizes, &err);
    ck_assert_msg(strstr(out, cases[i].code), "case %zu: %s", i, out);
  }
  report_into(out, sizeof out, 0, DVBSTREAM_LENGTH, in, sizeof in, sizes, &err);
  ck_assert_ptr_nonnull(strstr(out, "bitstream needs 100 bytes, 4 present"));
  report_into(out, sizeof out, 1, DVBSTREAM_LENGTH, in, sizeof in, sizes, &err);
  ck_assert_ptr_nonnull(strstr(out, "7+9 bytes"));
  report_into(out, sizeof out, 1, DVBSTREAM_RULE, in, sizeof in, sizes, &err);
  ck_assert_ptr_nonnull(strstr(out, "reason text"));
}
END_TEST

static bim_node_t *rich_main(void) {
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *pd = tn(doc, "ProgramDescription", NULL);
  bim_node_t *tbl = tn(pd, "ProgramInformationTable", NULL);
  bim_nsdecl_t *d = calloc(1, sizeof *d);
  bim_node_t *p;
  ck_assert_ptr_nonnull(d);
  d->prefix = "t";
  d->uri = TVA;
  doc->nsdecls = d;
  doc->nnsdecls = 1;
  ck_assert_int_eq(bim_node_add_attr_ns(doc, NULL, "publisher", strdup("pub")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(doc, "urn:other", "extra", strdup("x")), 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/b", "B")), 0);
  p = tn(tbl, NULL, "stray text");
  p->name = NULL;
  return doc;
}

START_TEST(tvafrag_allocation_failures) {
  tb_t t;
  bim_node_t *doc;
  bim_tvapart_t *parts = NULL;
  bim_tvapart_t *copy = NULL;
  bim_node_t *joined = NULL;
  size_t n = 0;
  tb_init(&t, 0, 0);
  doc = rich_main();
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n);
    hit = alloc_fail_end();
    if (!rc) bim_tvafrag_free(parts, n);
    if (!hit) break;
  }
  ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n), 0);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_tvafrag_copy(parts, n, &copy);
    hit = alloc_fail_end();
    if (!rc) bim_tvafrag_free(copy, n);
    if (!hit) break;
  }
  bim_tvafrag_free(parts, n);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    ck_assert_int_eq(bim_tvafrag_split(t.c, &t.ns, doc, &parts, &n), 0);
    alloc_fail_at(k);
    rc = bim_tvafrag_join(t.c, &t.ns, parts, n, &joined);
    hit = alloc_fail_end();
    if (!rc) bim_node_free(joined);
    bim_tvafrag_free(parts, n);
    if (!hit) break;
  }
  bim_node_free(doc);
  tb_free(&t);
}
END_TEST

START_TEST(tvafrag_join_builds_missing_containers_and_rejects_unknown_codes) {
  tb_t t;
  bim_tvapart_t parts[4];
  bim_node_t *joined = NULL;
  tb_init(&t, 0, 0);
  memset(parts, 0, sizeof parts);
  parts[0].code = 0x0001;
  parts[0].root = program("crid://example.com/a", "A");
  parts[1].code = 0x0001;
  parts[1].root = program("crid://example.com/b", "B");
  ck_assert_int_eq(bim_tvafrag_join(t.c, &t.ns, parts, 2, &joined), 0);
  ck_assert_str_eq(joined->name, "TVAMain");
  ck_assert_int_eq(joined->kids[0]->kids[0]->nkids, 2);
  bim_node_free(joined);

  parts[0].code = 0x000E;
  parts[0].root = tn(NULL, "TVAMain", NULL);
  parts[1].code = 0x000E;
  parts[1].root = tn(NULL, "TVAMain", NULL);
  parts[2].code = 0x0001;
  parts[2].root = program("crid://example.com/c", "C");
  parts[3].code = 0x0001;
  parts[3].root = NULL;
  ck_assert_int_eq(bim_tvafrag_join(t.c, &t.ns, parts, 4, &joined), 0);
  bim_node_free(joined);
  bim_node_free(parts[1].root);
  parts[1].root = NULL;

  parts[0].code = 0x0005;
  parts[0].root = tn(NULL, "Schedule", NULL);
  ck_assert_int_eq(bim_tvafrag_join(t.c, &t.ns, parts, 1, &joined), -3);
  bim_node_free(parts[0].root);
  parts[0].code = 0x0001;
  parts[0].root = NULL;
  {
    bim_tvans_t none = {NULL, NULL, NULL};
    bim_tvapart_t one = {0x0001, NULL};
    ck_assert_int_eq(bim_tvafrag_join(t.c, &none, &one, 1, &joined), -3);
  }
  tb_free(&t);
}
END_TEST

static void mk_fuus(bim_tvabim_fuu_t in[4]) {
  in[0] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_REPLACE, 0, program("crid://example.com/a", "A long programme title spanning chunks")};
  in[1] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_ADD, 7, program("crid://example.com/b", "Short")};
  in[2] = (bim_tvabim_fuu_t){0x0001, BIM_CMD_DELETE, 7, NULL};
  in[3] = (bim_tvabim_fuu_t){0x000E, BIM_CMD_REPLACE, 0, tn(NULL, "TVAMain", NULL)};
  ck_assert_int_eq(bim_node_add_attr_ns(in[3].root, NULL, "publisher", strdup("tvlistings.tv/TV%20Listings")), 0);
}

static void free_fuus(bim_tvabim_fuu_t in[4]) {
  for (int i = 0; i < 4; i++) bim_node_free(in[i].root);
}

START_TEST(tvabim_au_survives_failures_and_damage) {
  tb_t t;
  bim_tvabim_fuu_t in[4];
  bim_tvabim_fuu_t *got;
  bim_fragref_t refs[4];
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *b;
  unsigned char *copy;
  size_t bl;
  size_t n;
  for (int variant = 0; variant < 3; variant++) {
    tb_init(&t, variant == 1 ? 8 : 0, variant == 2);
    mk_fuus(in);
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 4, &bw, refs, &err), 0);
    b = bitwriter_data(&bw, &bl);
    copy = malloc(bl);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, b, bl);
    bitwriter_free(&bw);
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 0, &bw, NULL, &err), 0);
    bitwriter_free(&bw);
    free_fuus(in);

    for (long k = 0;; k++) {
      int hit;
      mk_fuus(in);
      bitwriter_init(&bw);
      alloc_fail_at(k);
      (void)bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 4, &bw, refs, &err);
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      free_fuus(in);
      if (!hit) break;
    }
    for (long k = 0;; k++) {
      int hit;
      int rc;
      got = NULL;
      n = 0;
      alloc_fail_at(k);
      rc = bim_tvabim_au_parse(t.c, &t.ns, &t.st, copy, bl, &got, &n, &err);
      hit = alloc_fail_end();
      if (!rc) bim_tvabim_au_free(got, n);
      if (!hit) break;
    }
    for (size_t cut = 0; cut < bl; cut++)
      if (!bim_tvabim_au_parse(t.c, &t.ns, &t.st, copy, cut, &got, &n, &err)) bim_tvabim_au_free(got, n);
    for (size_t bit = 0; bit < bl * 8; bit++) {
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      if (!bim_tvabim_au_parse(t.c, &t.ns, &t.st, copy, bl, &got, &n, &err)) bim_tvabim_au_free(got, n);
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    free(copy);
    tb_free(&t);
  }
}
END_TEST

START_TEST(tvabim_state_defaults_and_options) {
  tb_t t;
  bim_decode_opts_t o;
  bim_fuu_state_t st;
  tb_init(&t, 0, 0);
  bim_tvabim_state(t.c, &t.di, NULL, &st);
  ck_assert_int_eq(st.nschemas, 1);
  ck_assert_ptr_eq(st.schemas, &t.c->schema);
  memset(&o, 0, sizeof o);
  o.nschemas = 1;
  o.schemas = &t.c->schema;
  o.lax_schema = 3;
  o.mpegb = 1;
  bim_tvabim_state(t.c, &t.di, &o, &st);
  ck_assert_int_eq(st.opts.lax_schema, -1);
  ck_assert_int_eq(st.opts.mpegb, 0);
  tb_free(&t);
}
END_TEST

static int deflate_fail(const unsigned char *, size_t, unsigned char *, size_t *) { return -1; }

static int deflate_empty(const unsigned char *, size_t, unsigned char *, size_t *outlen) {
  *outlen = 0;
  return 0;
}

static int deflate_second_fails(const unsigned char *in, size_t n, unsigned char *out, size_t *outlen) {
  static int calls;
  if (++calls % 2 == 0) return -1;
  return ident(in, n, out, outlen);
}

START_TEST(tvabim_zlib_chunks_survive_failures) {
  static const size_t sizes[] = {1, 8, 64, 100000};
  for (int gather = 0; gather < 2; gather++)
    for (size_t si = 0; si < sizeof sizes / sizeof *sizes; si++) {
      tb_t t;
      bim_tvabim_fuu_t in[4];
      bitwriter_t bw;
      bim_err_t err;
      long span = sizes[si] > 1 && sizes[si] < 1000 ? 24 : 1;
      bim_deflate_fn fns[] = {NULL, deflate_fail, deflate_empty, deflate_second_fails};
      tb_init(&t, sizes[si], gather);
      for (size_t k = 0; k < sizeof fns / sizeof *fns; k++) {
        t.eo.deflate = fns[k];
        mk_fuus(in);
        bitwriter_init(&bw);
        ck_assert_int_eq(bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 4, &bw, NULL, &err), -1);
        bitwriter_free(&bw);
        free_fuus(in);
      }
      t.eo.deflate = ident;
      for (long n = 0;; n++) {
        int hit;
        mk_fuus(in);
        bitwriter_init(&bw);
        for (int k = 0; k < n % span; k++) ck_assert_int_eq(bitwriter_put(&bw, 0xA5, 8), 0);
        alloc_fail_at(n / span);
        (void)bim_tvabim_au_write(t.c, &t.ns, &t.st, &t.eo, in, 4, &bw, NULL, &err);
        hit = alloc_fail_end();
        bitwriter_free(&bw);
        free_fuus(in);
        if (!hit && n % span == span - 1) break;
      }
      tb_free(&t);
    }
}
END_TEST

static void head_for(const tb_t *t, const char *const *more, size_t nmore, bitwriter_t *hw) {
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_decoderinit_t di;
  bim_err_t err;
  bitwriter_init(hw);
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, more, nmore, &di), 0);
  ck_assert_int_eq(bim_tvainit_write(&ti, hw, &err), 0);
  ck_assert_int_eq(bim_decoderinit_write(&di, NULL, NULL, hw, &err), 0);
  bim_tvainit_decoderinit_free(&di);
  (void)t;
}

START_TEST(dvbstream_survives_damage_and_allocation_failure) {
  tb_t t;
  bim_node_t *doc = tn(NULL, "TVAMain", NULL);
  bim_node_t *tbl = tn(tn(doc, "ProgramDescription", NULL), "ProgramInformationTable", NULL);
  dvbtva_roots_t r;
  unsigned char *buf = NULL;
  unsigned char *copy;
  size_t len = 0;
  dvbstream_head_t head;
  size_t counts[DVBSTREAM_CODES] = {0};
  size_t sizes[2];
  bim_err_t err;
  bim_node_t *back = NULL;
  bitwriter_t hw;
  const unsigned char *hp;
  size_t hl;
  const char *many[DVBTVA_SCHEMAS + 1];
  char longuri[400];
  const char *longs[1];
  tb_init(&t, 0, 0);
  r = (dvbtva_roots_t){t.c, &t.ns};
  ck_assert_int_eq(bim_node_add(tbl, program("crid://example.com/a", "A")), 0);
  ck_assert_int_eq(dvbstream_write(&r, &t.eo, doc, 0, &buf, &len, counts, sizes, &err), DVBSTREAM_OK);
  copy = malloc(len);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, buf, len);

  for (long k = 0;; k++) {
    int hit;
    unsigned char *o2 = NULL;
    size_t l2 = 0;
    int rc;
    alloc_fail_at(k);
    rc = dvbstream_write(&r, &t.eo, doc, 1, &o2, &l2, NULL, sizes, &err);
    hit = alloc_fail_end();
    if (rc == DVBSTREAM_OK) free(o2);
    if (!hit) break;
  }
  ck_assert_int_eq(dvbstream_head(copy, len, &head, &err), DVBSTREAM_OK);
  for (long k = 0;; k++) {
    int hit;
    int rc;
    back = NULL;
    alloc_fail_at(k);
    rc = dvbstream_read(&r, &t.dop, copy + head.used, len - head.used, &back, NULL, &err);
    hit = alloc_fail_end();
    if (rc == DVBSTREAM_OK) bim_node_free(back);
    if (!hit) break;
  }
  for (size_t cut = 0; cut < len; cut++) {
    int rc = dvbstream_head(copy, cut, &head, &err);
    if (rc == DVBSTREAM_OK && cut >= head.used) {
      back = NULL;
      if (dvbstream_read(&r, &t.dop, copy + head.used, cut - head.used, &back, NULL, &err) == DVBSTREAM_OK) bim_node_free(back);
    }
  }
  for (size_t bit = 0; bit < len * 8; bit++) {
    int rc;
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    rc = dvbstream_head(copy, len, &head, &err);
    if (rc == DVBSTREAM_OK) {
      back = NULL;
      if (dvbstream_read(&r, &t.dop, copy + head.used, len - head.used, &back, NULL, &err) == DVBSTREAM_OK) bim_node_free(back);
    }
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }

  for (int i = 0; i < DVBTVA_SCHEMAS + 1; i++) many[i] = "urn:test:more";
  head_for(&t, many, DVBTVA_SCHEMAS, &hw);
  hp = bitwriter_data(&hw, &hl);
  ck_assert_int_eq(dvbstream_head(hp, hl, &head, &err), DVBSTREAM_HEAD);
  ck_assert_str_eq(err.code, BIM_DVBTVA_ERR);
  bitwriter_free(&hw);
  memset(longuri, 'u', sizeof longuri - 1);
  longuri[sizeof longuri - 1] = 0;
  longs[0] = longuri;
  head_for(&t, longs, 1, &hw);
  hp = bitwriter_data(&hw, &hl);
  ck_assert_int_eq(dvbstream_head(hp, hl, &head, &err), DVBSTREAM_HEAD);
  bitwriter_free(&hw);
  head_for(&t, NULL, 0, &hw);
  hp = bitwriter_data(&hw, &hl);
  ck_assert_int_eq(dvbstream_head(hp, hl, &head, &err), DVBSTREAM_OK);
  for (size_t cut = 0; cut < hl; cut++) (void)dvbstream_head(hp, cut, &head, &err);
  bitwriter_free(&hw);

  free(copy);
  free(buf);
  bim_node_free(doc);
  tb_free(&t);
}
END_TEST

START_TEST(tvadatetime_limits_and_buffers) {
  static const char *const good[] = {"2024-05-01T10:20:30Z", "2024-05-01T10:20:30.5Z", "2024-05-01T10:20:30.123Z", "2024-05-01T10:20:30.1230Z", "2024-05-01T10:20:30.123000Z",
                                     "2024-05-01T10:20:30+02:00", "2024-05-01T00:30:00+02:00", "2024-05-01T23:30:00-02:00", "2024-05-01T10:20:30", "2024-05-01T10:20:30.07Z"};
  static const char *const bad[] = {"", "garbage", "2024-05-01T10:20:30.1234Z", "2024-05-01T10:20:30.0001Z", "0000-01-01T00:00:00+23:00", "9999-12-31T23:59:59Z", "1800-01-01T00:00:00Z"};
  static const char *const dgood[] = {"2024-05-01", "1858-11-17", "2000-02-29"};
  static const char *const dbad[] = {"", "2024-5-1", "2024-13-01", "2024-00-10", "2024-01-32", "2024-01-00", "2024-02-30", "2024-05-01x", "1800-01-01", "x"};
  char out[40];
  for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
    bitwriter_t bw;
    const unsigned char *p;
    size_t n;
    bitreader_t br;
    bitwriter_init(&bw);
    ck_assert_msg(tva_datetime_encode(&bw, good[i]) == 0, "enc %s", good[i]);
    p = bitwriter_data(&bw, &n);
    for (size_t cap = 0; cap < 30; cap++) {
      bitreader_init(&br, p, n);
      (void)tva_datetime_decode(&br, out, cap);
    }
    bitreader_init(&br, p, n);
    ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), 0);
    bitwriter_free(&bw);
  }
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    bitwriter_t bw;
    bitwriter_init(&bw);
    (void)tva_datetime_encode(&bw, bad[i]);
    bitwriter_free(&bw);
  }
  for (size_t i = 0; i < sizeof dgood / sizeof dgood[0]; i++) {
    bitwriter_t bw;
    const unsigned char *p;
    size_t n;
    bitreader_t br;
    bitwriter_init(&bw);
    ck_assert_msg(tva_date_encode(&bw, dgood[i]) == 0, "enc %s", dgood[i]);
    p = bitwriter_data(&bw, &n);
    for (size_t cap = 0; cap < 12; cap++) {
      bitreader_init(&br, p, n);
      (void)tva_date_decode(&br, out, cap);
    }
    bitreader_init(&br, p, n);
    ck_assert_int_eq(tva_date_decode(&br, out, sizeof out), 0);
    ck_assert_str_eq(out, dgood[i]);
    bitwriter_free(&bw);
  }
  for (size_t i = 0; i < sizeof dbad / sizeof dbad[0]; i++) {
    bitwriter_t bw;
    bitwriter_init(&bw);
    ck_assert_msg(tva_date_encode(&bw, dbad[i]) == -1, "accepted %s", dbad[i]);
    bitwriter_free(&bw);
  }
  {
    static const unsigned char big_ms[8] = {0, 0, 0xEA, 0x60, 0x05, 0x26, 0x5C, 0x00};
    static const unsigned char big_mjd[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    static const unsigned char short_in[3] = {1, 2, 3};
    bitreader_t br;
    bitreader_init(&br, big_ms, sizeof big_ms);
    ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), -1);
    bitreader_init(&br, big_mjd, sizeof big_mjd);
    ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), -1);
    bitreader_init(&br, big_mjd, sizeof big_mjd);
    ck_assert_int_eq(tva_date_decode(&br, out, sizeof out), -1);
    bitreader_init(&br, short_in, sizeof short_in);
    ck_assert_int_eq(tva_datetime_decode(&br, out, sizeof out), -1);
    bitreader_init(&br, short_in, sizeof short_in);
    ck_assert_int_eq(tva_date_decode(&br, out, sizeof out), -1);
  }
}
END_TEST

START_TEST(sdnsbim_survives_damage_and_allocation_failure) {
  char dir[] = "/tmp/dvbipitools_sdnsdoc2_XXXXXX";
  char path[300];
  const char *paths[1] = {dir};
  const char *culprit = NULL;
  bim_sdnsdoc_t d;
  bim_node_t *sd;
  bim_node_t *sv;
  dvbtva_fuu_t in;
  bim_sdnsroots_t roots;
  bim_encode_opts_t eo;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_err_t err;
  bim_tvaroot_t root;
  unsigned char *copy;
  const unsigned char *p;
  size_t n;
  char *xml = NULL;
  size_t xl = 0;
  FILE *f;
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(path, sizeof path, "%s/sdns.xsd", dir);
  f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(schema_sdns, 1, strlen(schema_sdns), f), strlen(schema_sdns));
  fclose(f);
  ck_assert_int_eq(bim_sdnsdoc_open(&d, paths, 1, SDNS, &culprit), 0);
  d.inflate = ident;
  sd = bim_node_new("ServiceDiscovery");
  sd->ns = SDNS;
  sv = bim_node_add_new(sd, "Service", NULL);
  sv->ns = SDNS;
  ck_assert_int_eq(bim_node_set_attr(sv, "id", "svc1"), 0);
  bim_node_add_new(sv, "Name", "Only name")->ns = SDNS;
  in = (dvbtva_fuu_t){BIM_SDNS_CONTEXT_PATH, sd};
  roots = (bim_sdnsroots_t){d.c, SDNS};
  memset(&eo, 0, sizeof eo);
  eo.schemas = &d.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  eo.mpegb = 1;
  eo.deflate = ident;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(dvbtva_au_write(bim_sdnsbim_root, &roots, &eo, &in, 1, &bw, &sw, NULL, &err), 0);
  p = bitwriter_data(&bw, &n);
  copy = malloc(n);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  ck_assert_int_eq(bim_sdnsbim_root(&roots, 0x31, &root), -1);
  ck_assert_int_eq(bim_sdnsbim_root(&roots, BIM_SDNS_CONTEXT_PATH, &root), 0);
  for (size_t cut = 0; cut < n; cut++)
    if (!bim_sdnsdoc_to_xml(&d, copy, cut, &xml, &xl)) free(xml);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    if (!bim_sdnsdoc_to_xml(&d, copy, n, &xml, &xl)) free(xml);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  for (long k = 0;; k++) {
    int hit;
    int rc;
    xml = NULL;
    alloc_fail_at(k);
    rc = bim_sdnsdoc_to_xml(&d, copy, n, &xml, &xl);
    hit = alloc_fail_end();
    if (!rc) free(xml);
    if (!hit) break;
  }
  for (long k = 0;; k++) {
    int hit;
    int rc;
    bim_sdnsdoc_t d2;
    alloc_fail_at(k);
    rc = bim_sdnsdoc_open(&d2, paths, 1, SDNS, &culprit);
    hit = alloc_fail_end();
    if (!rc) bim_sdnsdoc_close(&d2);
    else xsd_set_free(d2.set);
    if (!hit) break;
  }
  free(copy);
  bim_node_free(sd);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bim_sdnsdoc_close(&d);
  unlink(path);
  rmdir(dir);
}
END_TEST

START_TEST(sdnsbim_decoderinit_survives_allocation_failure) {
  xsd_compile_opts_t o = {0, 1, 0, NULL, 0, 0};
  xsd_set_t *set = xsd_set_new();
  const xsd_compiled_t *c;
  xml_doc_t *doc = NULL;
  bim_decoderinit_t di;
  ck_assert_int_eq(xml_doc_parse(schema_sdns, strlen(schema_sdns), &doc, NULL), 0);
  if (xsd_set_add_doc(set, doc, "s.xsd") || xsd_set_resolve(set) || xsd_compile(set, &o, &c)) ck_abort_msg("%s", xsd_set_error(set));
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_sdnsbim_decoderinit(c, SDNS, &di);
    hit = alloc_fail_end();
    if (!rc) bim_sdnsbim_decoderinit_free(&di);
    if (!hit) break;
  }
  xsd_set_free(set);
}
END_TEST

static Suite *tvabim_suite(void) {
  Suite *s = suite_create("bim_tvabim");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, tvadatetime_matches_spec_examples);
  tcase_add_test(tc, tvadatetime_rejects_unrepresentable);
  tcase_add_test(tc, tvadate_matches_spec_example);
  tcase_add_test(tc, tvabim_fuu_bits_match_hand_coding);
  tcase_add_test(tc, tvabim_access_unit_round_trips);
  tcase_add_test(tc, tvafrag_split_and_join_round_trip);
  tcase_add_test(tc, tvafrag_names_roots_steps_and_codes);
  tcase_add_test(tc, tvans_detect_finds_extended_and_interstitial_namespaces);
  tcase_add_test(tc, tvafrag_copy_is_deep);
  tcase_add_test(tc, tvafrag_allocation_failures);
  tcase_add_test(tc, tvabim_au_survives_failures_and_damage);
  tcase_add_test(tc, tvabim_state_defaults_and_options);
  tcase_add_test(tc, tvabim_zlib_chunks_survive_failures);
  tcase_add_test(tc, dvbstream_survives_damage_and_allocation_failure);
  tcase_add_test(tc, tvadatetime_limits_and_buffers);
  tcase_add_test(tc, sdnsbim_survives_damage_and_allocation_failure);
  tcase_add_test(tc, sdnsbim_decoderinit_survives_allocation_failure);
  tcase_add_test(tc, tvafrag_join_builds_missing_containers_and_rejects_unknown_codes);
  tcase_add_test(tc, dvbstream_round_trips_a_document);
  tcase_add_test(tc, dvbstream_head_lists_every_schema_and_several_schemas_round_trip);
  tcase_add_test(tc, dvbstream_report_names_every_failure);
  tcase_add_test(tc, tvabim_gathering_shares_chunks);
  tcase_add_test(tc, tvabim_container_carries_access_unit);
  tcase_add_test(tc, tvabim_rejects_invalid_fragments);
  tcase_add_test(tc, sdnsdoc_decodes_a_segment_through_a_loaded_schema);
  tcase_add_test(tc, sdnsbim_segment_round_trips);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tvabim_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
