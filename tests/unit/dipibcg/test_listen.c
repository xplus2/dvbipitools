#define _GNU_SOURCE
/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../run_helper.h"
#include "../bcg_schema_fixture.h"
#include "dipibcg/listen.h"
#include "dipibcg/schema.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bcgip.h"
#include "lib/bim/container.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/index.h"
#include "lib/bim/strrepo.h"
#include "lib/net/dvbstp.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"
#include "lib/tva/tva_xml.h"

START_TEST(write_csvmap_quotes_fields_with_commas_and_quotes) {
  char path[] = "/tmp/dvbipitools_test_listen_csvmap_XXXXXX";
  bcg_doc_t doc;
  bcg_channel_t *c;
  FILE *f;
  char line[256];
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "a,b\"c");
  bufcpy(c->uri, sizeof c->uri, "rtp://h/x,y");
  c->tsid = 1;
  c->onid = 2;
  c->sid = 3;
  write_csvmap(path, &doc);
  f = fopen(path, "r");
  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(fgets(line, sizeof line, f));
  ck_assert_str_eq(line, "\"a,b\"\"c\",\"rtp://h/x,y\",1,2,3\n");
  fclose(f);
  bcg_doc_free(&doc);
  unlink(path);
}
END_TEST

START_TEST(write_csvmap_writes_only_channels_with_uri) {
  char path[] = "/tmp/dvbipitools_test_listen_csvmap_XXXXXX";
  bcg_doc_t doc;
  bcg_channel_t *c;
  FILE *f;
  char line[256];
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);

  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "channel2"); /* no uri: excluded */

  write_csvmap(path, &doc);

  f = fopen(path, "r");
  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(fgets(line, sizeof line, f));
  ck_assert_str_eq(line, "channel1,rtp://239.1.1.1:5000,1,2,101\n");
  ck_assert_ptr_null(fgets(line, sizeof line, f));
  fclose(f);

  bcg_doc_free(&doc);
  unlink(path);
}
END_TEST

#define GARBAGE_LEN 40

typedef struct {
  dvbstp_sender_t sd;
  char dir[64];
  config_t cfg;
  bcg_schema_t sch;
  unsigned char *valid;
  char *plain; /* TVA XML of data container and TVAMain */
  char *plain_main;
  unsigned char *tva_main;
  unsigned char *indexed[3]; /* data container with refs, index list, sub-index */
  unsigned char *split[2]; /* ProgramInformation, then Schedule + ServiceInformation in own container */
  unsigned char *ext_init; /* DVB-TVA-init listing urn:test:ext too */
  unsigned char *ext_data; /* data container coded for those schemas */
  char crid[TVA_CRID_BUF];
  unsigned char garbage[GARBAGE_LEN];
} bcg_sender_t;

static void make_doc(bcg_doc_t *doc) {
  bcg_channel_t *c;
  bcg_programme_t *pr;
  bcg_doc_init(doc);
  c = bcg_add_channel(doc);
  bufcpy(c->id, sizeof c->id, "ch1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  bufcpy(c->names[0], sizeof c->names[0], "Channel One");
  c->name_count = 1;
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  pr = bcg_add_programme(doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "ch1");
  bufcpy(pr->start, sizeof pr->start, "2030-01-01T12:00:00Z");
  bufcpy(pr->stop, sizeof pr->stop, "2030-01-01T13:00:00Z");
  bcg_set_text(&pr->title, "News");
}

static void frag_refs(const bcgau_ref_t *refs, int n, bim_fragref_t *out) {
  for (int i = 0; i < n; i++) out[i] = (bim_fragref_t){.au_off = refs[i].au_off, .str_off = refs[i].str_off, .frag_id = refs[i].frag_id, .version = refs[i].version};
}

/* docs[5]: data container with fragment ids 1.., docs[6] index list (container 0), docs[7] PI-by-CRID sub-index (container 2) */
static void build_indexed(bcg_sender_t *b) {
  bcg_doc_t doc;
  bcgau_scratch_t sc;
  bcg_doc_index_t dix;
  bim_fragref_t fr[8];
  bitwriter_t bw;
  strrepo_writer_t sw;
  bim_index_input_t in[BIM_INDEX_KINDS];
  bim_index_cont_t *conts;
  unsigned char *cont;
  size_t cont_len;
  size_t bits_len;
  size_t strs_len;
  const unsigned char *bits;
  const unsigned char *strs;
  int nfuu = 0;
  int n = 0;
  make_doc(&doc);
  memset(in, 0, sizeof in);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bcg_doc_index_build(&dix, &doc), 0);
  ck_assert_int_eq(bcgau_encode_range(&sc, &b->sch.au, &doc, &dix, 0, doc.channel_count, 0, &bw, &sw, &nfuu, NULL), 0);
  bcg_doc_index_free(&dix);
  ck_assert_int_le(nfuu, 8);
  for (int i = 0; i < nfuu; i++) {
    sc.refs[i].frag_id = (uint32_t)(i + 1);
    if (sc.refs[i].kind != BCGAU_PROGRAM) continue;
    char buf[TVA_CRID_BUF];
    bufcpy(b->crid, sizeof b->crid, tva_programme_crid(&doc.programmes[sc.refs[i].index], buf, sizeof buf));
    ck_assert_int_eq(bim_index_add(&in[BIM_INDEX_PI_CRID], b->crid, NULL, 1, sc.refs[i].frag_id), 0);
  }
  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  frag_refs(sc.refs, nfuu, fr);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, fr, nfuu, &cont, &cont_len), 0);
  ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, NULL, &b->indexed[0], &b->sd.lens[5]), 0);
  free(cont);
  ck_assert_int_eq(bim_index_build(1u << BIM_INDEX_PI_CRID, in, 2, &conts, &n), 0);
  ck_assert_int_eq(n, 2);
  for (int i = 0; i < 2; i++) {
    ck_assert_int_eq(bim_container_build_structs(conts[i].structs, conts[i].nstructs, &cont, &cont_len), 0);
    ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, NULL, &b->indexed[1 + i], &b->sd.lens[6 + i]), 0);
    free(cont);
  }
  for (int i = 0; i < 3; i++) b->sd.docs[5 + i] = b->indexed[i];
  bim_index_conts_free(conts, n);
  bim_index_input_free(&in[BIM_INDEX_PI_CRID]);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcgau_scratch_free(&sc);
  bcg_doc_free(&doc);
}

/* docs[8]: data container as plain TVA XML, docs[9]: TVAMain */
static void build_plain_docs(bcg_sender_t *b) {
  bcg_doc_t doc;
  bcg_doc_index_t dix;
  bcgau_frags_t fr;
  size_t xl;
  make_doc(&doc);
  ck_assert_int_eq(bcg_doc_index_build(&dix, &doc), 0);
  ck_assert_int_eq(bcgau_frags_range(&b->sch.au, &doc, &dix, 0, doc.channel_count, 0, &fr, NULL), 0);
  ck_assert_int_eq(bcgau_xml(&b->sch.au, fr.f, fr.n, &b->plain, &xl), 0);
  bcgau_frags_free(&fr);
  b->sd.docs[8] = (const unsigned char *)b->plain;
  b->sd.lens[8] = xl;
  bcg_doc_index_free(&dix);
  bcg_doc_free(&doc);
  bcg_doc_init(&doc);
  bufcpy(doc.publisher, sizeof doc.publisher, "example.com/My%20Guide");
  bufcpy(doc.lang, sizeof doc.lang, "de");
  ck_assert_int_eq(bcgau_frags_tva_main(&b->sch.au, &doc, &fr), 0);
  ck_assert_int_eq(bcgau_xml(&b->sch.au, fr.f, fr.n, &b->plain_main, &xl), 0);
  bcgau_frags_free(&fr);
  b->sd.docs[9] = (const unsigned char *)b->plain_main;
  b->sd.lens[9] = xl;
  bcg_doc_free(&doc);
}

/* docs[10]: ProgramInformation alone, docs[11]: Schedule + ServiceInformation of it */
static void build_split(bcg_sender_t *b) {
  bcg_doc_t doc;
  bcg_doc_index_t dix;
  bcgau_frags_t fr;
  bcgau_scratch_t sc;
  make_doc(&doc);
  ck_assert_int_eq(bcg_doc_index_build(&dix, &doc), 0);
  ck_assert_int_eq(bcgau_frags_range(&b->sch.au, &doc, &dix, 0, doc.channel_count, 0, &fr, NULL), 0);
  ck_assert_uint_eq(fr.n, 3);
  ck_assert_uint_eq(fr.kind[0], BCGAU_PROGRAM);
  bcgau_scratch_init(&sc);
  for (int k = 0; k < 2; k++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    unsigned char *cont;
    size_t cont_len;
    size_t bits_len;
    size_t strs_len;
    const unsigned char *bits;
    const unsigned char *strs;
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    ck_assert_int_eq(bcgau_write(&sc, &b->sch.au, fr.f + k, k ? 2 : 1, fr.chan + k, &bw, &sw), 0);
    bits = bitwriter_data(&bw, &bits_len);
    strs = strrepo_writer_data(&sw, &strs_len);
    ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, NULL, 0, &cont, &cont_len), 0);
    ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, NULL, &b->split[k], &b->sd.lens[10 + k]), 0);
    b->sd.docs[10 + k] = b->split[k];
    free(cont);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
  bcgau_scratch_free(&sc);
  bcgau_frags_free(&fr);
  bcg_doc_index_free(&dix);
  bcg_doc_free(&doc);
}

static const char ext_xsd[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:test:ext\"><xs:element name=\"Extra\" type=\"xs:string\"/></xs:schema>";

/* docs[12]: DVB-TVA-init with SchemaURI[1] urn:test:ext, its schema in b->dir. docs[13]: data container coded for both */
static void build_ext_init(bcg_sender_t *b) {
  const char *uris[2] = {BCG_FIXTURE_NS, "urn:test:ext"};
  bcg_schema_t s;
  bcg_doc_t doc;
  bcg_doc_index_t dix;
  bcgau_scratch_t sc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  unsigned char *cont;
  size_t cont_len;
  size_t bits_len;
  size_t strs_len;
  const unsigned char *bits;
  const unsigned char *strs;
  int nfuu = 0;
  bcg_fixture_put(b->dir, "ext.xsd", ext_xsd);
  ck_assert_int_eq(bcg_schema_open(&s, &b->cfg, BCG_FIXTURE_NS, 1), 0);
  ck_assert_int_eq(bcg_schema_extras(&s, &b->cfg, uris, 2, 1), 0);
  ck_assert_int_eq(bcg_schema_wire(&s, uris, 2), 0);
  ck_assert_int_eq(s.au.eo.multi, 1);
  b->ext_init = malloc(s.init_len);
  ck_assert_ptr_nonnull(b->ext_init);
  memcpy(b->ext_init, s.init, s.init_len);
  b->sd.docs[12] = b->ext_init;
  b->sd.lens[12] = s.init_len;
  make_doc(&doc);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcg_doc_index_build(&dix, &doc), 0);
  ck_assert_int_eq(bcgau_encode_range(&sc, &s.au, &doc, &dix, 0, doc.channel_count, 0, &bw, &sw, &nfuu, NULL), 0);
  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, NULL, 0, &cont, &cont_len), 0);
  ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, NULL, &b->ext_data, &b->sd.lens[13]), 0);
  b->sd.docs[13] = b->ext_data;
  free(cont);
  bcg_doc_index_free(&dix);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcgau_scratch_free(&sc);
  bcg_doc_free(&doc);
  bcg_schema_close(&s);
}

static void build_wrapped(bcg_sender_t *b, int compress) {
  bcg_doc_t doc;
  bcgau_scratch_t sc;
  bcg_doc_index_t dix;
  bim_fragref_t fr[8];
  bitwriter_t bw;
  strrepo_writer_t sw;
  unsigned char *cont;
  size_t cont_len;
  size_t bits_len;
  size_t strs_len;
  const unsigned char *bits;
  const unsigned char *strs;
  int nfuu = 0;

  bcg_fixture_make(b->dir);
  memset(&b->cfg, 0, sizeof b->cfg);
  b->cfg.schemas[0] = b->dir;
  b->cfg.nschemas = 1;
  ck_assert_int_eq(bcg_schema_open(&b->sch, &b->cfg, BCG_FIXTURE_NS, 1), 0);
  make_doc(&doc);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bcg_doc_index_build(&dix, &doc), 0);
  ck_assert_int_eq(bcgau_encode_range(&sc, &b->sch.au, &doc, &dix, 0, doc.channel_count, 0, &bw, &sw, &nfuu, NULL), 0);
  bcg_doc_index_free(&dix);
  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, NULL, 0, &cont, &cont_len), 0);
  ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, compress ? BCG_DEFLATE : NULL, &b->valid, &b->sd.lens[0]), 0);
  b->sd.docs[0] = b->valid;
  free(cont);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcgau_scratch_free(&sc);
  bcg_doc_free(&doc);
  memset(b->garbage, 0, sizeof b->garbage);
  b->garbage[0] = 0;
  memset(b->garbage + 1, 0x5A, sizeof b->garbage - 1);
  b->sd.docs[1] = b->garbage;
  b->sd.lens[1] = sizeof b->garbage;
  b->sd.docs[2] = b->sch.init;
  b->sd.lens[2] = b->sch.init_len;
  b->sd.docs[3] = b->garbage;
  b->sd.lens[3] = 8;
  /* TS 102 539 7.4 TVAMain data container */
  bcg_doc_init(&doc);
  bufcpy(doc.publisher, sizeof doc.publisher, "example.com/My%20Guide");
  bufcpy(doc.lang, sizeof doc.lang, "de");
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  ck_assert_int_eq(bcgau_encode_tva_main(&sc, &b->sch.au, &doc, &bw, &sw), 0);
  sc.refs[0].frag_id = 1;
  frag_refs(sc.refs, 1, fr);
  bits = bitwriter_data(&bw, &bits_len);
  strs = strrepo_writer_data(&sw, &strs_len);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, fr, 1, &cont, &cont_len), 0);
  ck_assert_int_eq(bim_bcg_wrap(cont, cont_len, compress ? BCG_DEFLATE : NULL, &b->tva_main, &b->sd.lens[4]), 0);
  b->sd.docs[4] = b->tva_main;
  free(cont);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcgau_scratch_free(&sc);
  build_indexed(b);
  build_plain_docs(b);
  build_split(b);
  build_ext_init(b);
}

#define TVA_INIT_STEP {DVBSTP_PAYLOAD_BCG_TVA_INIT, 0, 2, 1}
static const send_step_t bcg_valid[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_garbage_then_valid_with_noise[] = {
  TVA_INIT_STEP,
  {DVBSTP_PAYLOAD_BROADCAST_DISCOVERY, 1, 0, 0},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1},
  {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 2, 0, 1},
};
static const send_step_t bcg_garbage_only[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 1, 1}};
static const send_step_t bcg_no_init[] = {{DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_unknown_init[] = {{DVBSTP_PAYLOAD_BCG_TVA_INIT, 0, 3, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_with_tva_main[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_TVAMAIN, 0, 4, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_bad_tva_main[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_TVAMAIN, 0, 1, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}};
static const send_step_t bcg_indexed[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 5, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 6, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 7, 1}};
static const send_step_t bcg_index_unresolved[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 6, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 7, 1}};
static const send_step_t bcg_index_no_list[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 5, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 7, 1}};
/* index list in container 0x0000, TS 102 323 9.2.2 */
static const unsigned seg_indexed[] = {0, 1, 0, 2};
static const unsigned seg_index_no_list[] = {0, 1, 2};
static const send_step_t bcg_two_versions[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 5, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 2, 5, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 6, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 2, 6, 1}, {DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, 1, 7, 1}};
static const unsigned seg_two_versions[] = {0, 1, 1, 0, 0, 2};
static const send_step_t bcg_plain[] = {{DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 8, 0}};
static const send_step_t bcg_plain_with_main[] = {{DVBSTP_PAYLOAD_BCG_TVAMAIN, 0, 9, 0}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 8, 0}};
static const send_step_t bcg_split[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 10, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 11, 1}};
static const unsigned seg_split[] = {0, 1, 2};
static const send_step_t bcg_ext_init[] = {{DVBSTP_PAYLOAD_BCG_TVA_INIT, 0, 12, 1}, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 13, 1}};
static const send_step_t bcg_container_compr_0[] = {TVA_INIT_STEP, {DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, 0, 0}};

typedef struct {
  const send_step_t *steps;
  unsigned nsteps;
  int compress;
  int with_csvmap;
  int bad_output;
  int other_ns; /* -s names a schema file of another namespace */
  int rc;
  const char *log_has;
  const char *out_has;
  const unsigned *segment_ids;
  bcg_fmt_t format; /* output */
  int until_signal; /* -t 0 */
} bcg_listen_case_t;

static const bcg_listen_case_t bcg_listen_cases[] = {
  {bcg_valid, 2, 0, 1, 0, 0, 0, "captured 1 time in 1 segment", "<channel id=\"ch1\"", NULL, FMT_XMLTV, 0},
  {bcg_valid, 2, 0, 0, 0, 0, 0, "until stopped", "<channel id=\"ch1\"", NULL, FMT_XMLTV, 1},
  /* sender repeats older versions: latest version per segment kept, a return to an older one is new */
  {bcg_garbage_then_valid_with_noise, 5, 0, 0, 0, 0, 0, "captured 2 times in 4 segments", "<title", NULL, FMT_XMLTV, 0},
  {bcg_garbage_only, 2, 0, 0, 0, 0, 1, "captured 0 times in 1 segment", NULL, NULL, FMT_XMLTV, 0},
  {bcg_valid, 2, 0, 0, 1, 0, 1, "cannot open", NULL, NULL, FMT_XMLTV, 0},
  {bcg_valid, 2, 0, 0, 2, 0, 1, "error writing", NULL, NULL, FMT_XMLTV, 0},
  {bcg_no_init, 1, 0, 0, 0, 0, 1, "captured 0 times in 0 segments", NULL, NULL, FMT_XMLTV, 0},
  {bcg_unknown_init, 2, 0, 0, 0, 0, 1, "GW400240", NULL, NULL, FMT_XMLTV, 0},
  {bcg_container_compr_0, 2, 0, 0, 0, 0, 1, "captured 0 times in 1 segment", NULL, NULL, FMT_XMLTV, 0},
  {bcg_with_tva_main, 3, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<tv source-info-name=\"My Guide\"", NULL, FMT_XMLTV, 0},
  {bcg_with_tva_main, 3, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<title lang=\"de\">News</title>", NULL, FMT_XMLTV, 0},
  {bcg_bad_tva_main, 3, 0, 0, 0, 0, 0, "GW400245", "<tv generator-info-name=", NULL, FMT_XMLTV, 0},
  {bcg_indexed, 4, 0, 0, 0, 0, 0, "index check: 1 indices ok, 1 entries", "<title", seg_indexed, FMT_XMLTV, 0},
  {bcg_index_unresolved, 4, 0, 0, 0, 0, 0, "GW400247", NULL, seg_indexed, FMT_XMLTV, 0},
  {bcg_index_no_list, 3, 0, 0, 0, 0, 0, "GW400246", NULL, seg_index_no_list, FMT_XMLTV, 0},
  {bcg_two_versions, 6, 0, 0, 0, 0, 0, "captured 4 times in 4 segments", "<title", seg_two_versions, FMT_XMLTV, 0},
  {bcg_plain, 1, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<channel id=\"ch1\"", NULL, FMT_XMLTV, 0},
  {bcg_plain_with_main, 2, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<tv source-info-name=\"My Guide\"", NULL, FMT_XMLTV, 0},
  {bcg_plain, 1, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<ServiceInformation serviceId=\"ch1\"", NULL, FMT_TVA, 0},
  {bcg_valid, 2, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "<Title>News</Title>", NULL, FMT_TVA, 0},
  {bcg_with_tva_main, 3, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "Channel One", NULL, FMT_BIM, 0},
  /* schedule texts from programme of another container */
  {bcg_split, 3, 0, 0, 0, 0, 0, "captured 2 times in 2 segments", "<title>News</title>", seg_split, FMT_XMLTV, 0},
  /* -f bim of a multi-schema stream: DecoderInit lists its known schemas */
  {bcg_ext_init, 2, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "urn:test:ext", NULL, FMT_BIM, 0},
  {bcg_plain, 1, 0, 0, 0, 0, 0, "captured 1 time in 1 segment", "Channel One", NULL, FMT_BIM, 0},
#ifdef DIPIBCG_TEST_ZLIB
  {bcg_valid, 2, 1, 1, 0, 0, 0, "captured 1 time in 1 segment", "<channel id=\"ch1\"", NULL, FMT_XMLTV, 0},
#endif
  {bcg_valid, 2, 0, 0, 0, 1, 1, "GW400240", NULL, NULL, FMT_XMLTV, 0},
};

START_TEST(listen_run_captures_a_wrapped_container_and_skips_bad_segments) {
  const bcg_listen_case_t *c = &bcg_listen_cases[_i];
  bcg_sender_t *b = calloc(1, sizeof *b);
  char group[32];
  char out_path[64];
  char map_path[64];
  char other_path[100];
  char msg[8192];
  char text[16384];
  pthread_t th;
  config_t cfg;
  FILE *f;
  size_t n;
  int fd;
  int rc;

  ck_assert_ptr_nonnull(b);
  build_wrapped(b, c->compress);
  if (c->compress) ck_assert_uint_eq(b->valid[0], 1);
  run_helper_group(group, sizeof group, 83);
  b->sd.group = group;
  b->sd.port = run_helper_free_udp_port();
  b->sd.steps = c->steps;
  b->sd.nsteps = c->nsteps;
  b->sd.segment_ids = c->segment_ids;
  snprintf(out_path, sizeof out_path, "/tmp/dipibcg_listen_XXXXXX");
  fd = mkstemp(out_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  snprintf(map_path, sizeof map_path, "/tmp/dipibcg_listenmap_XXXXXX");
  fd = mkstemp(map_path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "%s", group);
  cfg.mcast_port = b->sd.port;
  cfg.timeout_s = c->until_signal ? 0 : 20;
  cfg.output_path = c->bad_output == 2 ? "/dev/full" : c->bad_output ? "/nonexistent-dir/out.xml" : out_path;
  cfg.csvmap_path = c->with_csvmap ? map_path : NULL;
  cfg.verbose = 1;
  cfg.format = c->format;
  cfg.schemas[0] = b->dir;
  cfg.nschemas = 1;
  if (c->other_ns) {
    char *p;
    char other[sizeof bcg_fixture_tva_xsd];
    snprintf(other, sizeof other, "%s", bcg_fixture_tva_xsd);
    while ((p = strstr(other, "metadata:2004"))) p[12] = '5', p[11] = '0', p[10] = '0', p[9] = '2';
    bcg_fixture_put(b->dir, "other.xsd", other);
    snprintf(other_path, sizeof other_path, "%s/other.xsd", b->dir);
    cfg.schemas[0] = other_path;
  }
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, dvbstp_sender_thread, &b->sd), 0);
  rc = listen_run(&cfg);
  pthread_join(th, NULL);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(rc, c->rc);
  ck_assert_ptr_nonnull(strstr(msg, c->log_has));
  if (c->out_has) {
    f = fopen(out_path, "r");
    ck_assert_ptr_nonnull(f);
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    ck_assert_ptr_nonnull(memmem(text, n, c->out_has, strlen(c->out_has)));
  }
  if (c->with_csvmap) {
    f = fopen(map_path, "r");
    ck_assert_ptr_nonnull(f);
    n = fread(text, 1, sizeof text - 1, f);
    text[n] = '\0';
    fclose(f);
    ck_assert_ptr_nonnull(strstr(text, "ch1,rtp://239.1.1.1:5000,1,2,101"));
  }
  bcg_schema_close(&b->sch);
  {
    char p[100];
    snprintf(p, sizeof p, "%s/ext.xsd", b->dir);
    unlink(p);
  }
  if (c->other_ns) {
    char p[100];
    snprintf(p, sizeof p, "%s/other.xsd", b->dir);
    unlink(p);
  }
  bcg_fixture_remove(b->dir);
  free(b->valid);
  free(b->plain);
  free(b->plain_main);
  free(b->tva_main);
  for (int i = 0; i < 3; i++) free(b->indexed[i]);
  for (int i = 0; i < 2; i++) free(b->split[i]);
  free(b->ext_init);
  free(b->ext_data);
  free(b);
  unlink(out_path);
  unlink(map_path);
}
END_TEST

START_TEST(write_csvmap_reports_unwritable_paths_and_line_breaks) {
  char path[] = "/tmp/dvbipitools_test_listen_csvbreak_XXXXXX";
  bcg_doc_t doc;
  bcg_channel_t *c;
  char line[256];
  FILE *f;
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "bad\nid");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "ok");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.2:5000\r");
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "fine");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.3:5000");
  write_csvmap("/nonexistent-dir/map.csv", &doc);
  write_csvmap("/dev/full", &doc);
  write_csvmap(path, &doc);
  f = fopen(path, "r");
  ck_assert_ptr_nonnull(f);
  ck_assert_ptr_nonnull(fgets(line, sizeof line, f));
  ck_assert_ptr_nonnull(strstr(line, "fine"));
  ck_assert_ptr_null(fgets(line, sizeof line, f));
  fclose(f);
  bcg_doc_free(&doc);
  unlink(path);
}
END_TEST

START_TEST(listen_run_fails_to_join_a_non_multicast_group) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "10.1.2.3");
  cfg.mcast_port = 45000;
  cfg.timeout_s = 1;
  cfg.output_path = "/tmp/dipibcg_never";
  ck_assert_int_eq(listen_run(&cfg), 1);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipibcg_listen");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, write_csvmap_quotes_fields_with_commas_and_quotes);
  tcase_add_test(tc, write_csvmap_writes_only_channels_with_uri);
  tcase_add_test(tc, write_csvmap_reports_unwritable_paths_and_line_breaks);
  tcase_add_test(tc, listen_run_fails_to_join_a_non_multicast_group);
  tcase_add_loop_test(tc, listen_run_captures_a_wrapped_container_and_skips_bad_segments, 0, (int)(sizeof bcg_listen_cases / sizeof bcg_listen_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(listen_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
