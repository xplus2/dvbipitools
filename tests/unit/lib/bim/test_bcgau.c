/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "../../bcg_schema_fixture.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bcgfrag.h"
#include "lib/sys/ioutil.h"
#include "lib/tva/tva_xml.h"

typedef struct {
  char dir[64];
  xsd_set_t *set;
  bcgau_t au;
} fx_t;

static void fx_open(fx_t *f) {
  const xsd_compiled_t *c = NULL;
  const char *culprit;
  const char *paths[1] = {f->dir};
  bcg_fixture_make(f->dir);
  f->set = xsd_set_new();
  ck_assert_ptr_nonnull(f->set);
  if (xsd_load_for_ns(f->set, NULL, paths, 1, BCG_FIXTURE_NS, &culprit, NULL, 0) || dvbtva_compile(f->set, BCG_FIXTURE_NS, &c))
    ck_abort_msg("%s %s", xsd_set_error_code(f->set), xsd_set_error(f->set));
  bcgau_init(&f->au, c, BCG_FIXTURE_NS, NULL);
}

static void fx_close(fx_t *f) {
  xsd_set_free(f->set);
  bcg_fixture_remove(f->dir);
}

static void make_doc(bcg_doc_t *doc) {
  bcg_channel_t *c;
  bcg_programme_t *p;
  bcg_doc_init(doc);
  c = bcg_add_channel(doc);
  bufcpy(c->id, sizeof c->id, "ch1");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.1:5000");
  bufcpy(c->names[0], sizeof c->names[0], "Channel One");
  bufcpy(c->names[1], sizeof c->names[1], "Kanal Eins");
  c->name_count = 2;
  c->tsid = 1;
  c->onid = 2;
  c->sid = 101;
  c = bcg_add_channel(doc);
  bufcpy(c->id, sizeof c->id, "ch2");
  bufcpy(c->uri, sizeof c->uri, "rtp://239.1.1.2:5000");
  p = bcg_add_programme(doc);
  bufcpy(p->channel_id, sizeof p->channel_id, "ch1");
  bufcpy(p->start, sizeof p->start, "2030-01-01T12:00:00Z");
  bufcpy(p->stop, sizeof p->stop, "2030-01-01T13:30:00Z");
  bcg_set_text(&p->title, "News");
  bcg_set_text(&p->desc, "Evening news");
  bcg_set_text(&p->category, "News");
  p = bcg_add_programme(doc);
  bufcpy(p->channel_id, sizeof p->channel_id, "ch1");
  bufcpy(p->start, sizeof p->start, "2030-01-01T13:30:00Z");
  bcg_set_text(&p->title, "Weather");
}

static const bcg_programme_t *find_prog(const bcg_doc_t *d, const char *title) {
  for (int i = 0; i < d->programme_count; i++)
    if (d->programmes[i].title && !strcmp(d->programmes[i].title, title)) return &d->programmes[i];
  return NULL;
}

/* programmes with text, times with duration instead of end, channels with names and triplet, TVAMain */
START_TEST(bcgau_range_round_trips_through_the_schema) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcgau_scratch_t sc;
  bim_fragref_t fr[16];
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  int nfuu = 0;
  int ndec = 0;
  fx_open(&f);
  make_doc(&in);
  bcg_doc_init(&out);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_encode_range(&sc, &f.au, &in, &ix, 0, in.channel_count, 0, &bw, &sw, &nfuu, NULL), 0);
  ck_assert_int_eq(nfuu, 5);
  ck_assert_int_eq(sc.refs[0].kind, BCGAU_PROGRAM);
  ck_assert_int_eq(sc.refs[2].kind, BCGAU_SCHEDULE);
  ck_assert_int_eq(sc.refs[3].kind, BCGAU_SERVICE);
  for (int i = 0; i < nfuu; i++) fr[i] = (bim_fragref_t){.au_off = sc.refs[i].au_off, .str_off = sc.refs[i].str_off};
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(bcgau_decode_refs(&f.au, &br, &sr, fr, nfuu, &out, &ndec), 0);
  ck_assert_int_eq(ndec, 5);
  ck_assert_int_eq(out.channel_count, 2);
  ck_assert_str_eq(out.channels[0].id, "ch1");
  ck_assert_str_eq(out.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_int_eq(out.channels[0].name_count, 2);
  ck_assert_str_eq(out.channels[0].names[1], "Kanal Eins");
  ck_assert_uint_eq(out.channels[0].sid, 101u);
  ck_assert_uint_eq(out.channels[0].onid, 2u);
  ck_assert_int_eq(out.channels[1].name_count, 0);
  ck_assert_int_eq(out.programme_count, 2);
  {
    const bcg_programme_t *p = find_prog(&out, "News");
    ck_assert_ptr_nonnull(p);
    ck_assert_str_eq(p->channel_id, "ch1");
    ck_assert_str_eq(p->start, "2030-01-01T12:00:00Z");
    ck_assert_str_eq(p->stop, "2030-01-01T13:30:00Z");
    ck_assert_str_eq(p->desc, "Evening news");
    ck_assert_str_eq(p->category, "News");
    ck_assert_ptr_nonnull(find_prog(&out, "Weather"));
  }
  bcg_doc_free(&out);
  bcg_doc_index_free(&ix);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_tva_main_carries_publisher_and_lang) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcgau_scratch_t sc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  int n = 0;
  fx_open(&f);
  bcg_doc_init(&in);
  bcg_doc_init(&out);
  bufcpy(in.publisher, sizeof in.publisher, "example.com/My%20Guide");
  bufcpy(in.lang, sizeof in.lang, "de-AT");
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcgau_encode_tva_main(&sc, &f.au, &in, &bw, &sw), 0);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(bcgau_decode_refs(&f.au, &br, &sr, NULL, 0, &out, &n), 0);
  ck_assert_str_eq(out.publisher, "example.com/My%20Guide");
  ck_assert_str_eq(out.lang, "de-AT");
  in.publisher[0] = '\0';
  bitwriter_reset(&bw);
  ck_assert_int_eq(bcgau_encode_tva_main(&sc, &f.au, &in, &bw, &sw), -1);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_decode_rejects_truncated_access_unit) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcgau_scratch_t sc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  int nfuu = 0;
  int bad = 0;
  fx_open(&f);
  make_doc(&in);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_encode_range(&sc, &f.au, &in, &ix, 0, in.channel_count, 0, &bw, &sw, &nfuu, NULL), 0);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  for (size_t cut = 0; cut < al; cut++) {
    int n = 0;
    bcg_doc_init(&out);
    bitreader_init(&br, a, cut);
    ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
    bad += bcgau_decode_refs(&f.au, &br, &sr, NULL, 0, &out, &n) != 0;
    bcg_doc_free(&out);
  }
  ck_assert_int_gt(bad, 0);
  bcg_doc_index_free(&ix);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

static int lookup_title(void *ctx, const char *crid, bcg_programme_t *pr) {
  (void)crid;
  (*(int *)ctx)++;
  return bcg_set_text(&pr->title, "Looked up") ? -2 : 0;
}

START_TEST(bcgfrag_builders_and_readers_round_trip) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcg_programme_t pr;
  bcg_channel_t ch;
  char crid[TVA_CRID_BUF];
  char want[TVA_CRID_BUF];
  bim_node_t *n;
  int calls = 0;
  fx_open(&f);
  make_doc(&in);
  bcg_doc_init(&out);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);

  n = bcgfrag_program(&f.au, &in.programmes[0]);
  ck_assert_ptr_nonnull(n);
  memset(&pr, 0, sizeof pr);
  ck_assert_int_eq(bcgfrag_read_program(n, crid, sizeof crid, &pr), 0);
  ck_assert_str_eq(crid, tva_programme_crid(&in.programmes[0], want, sizeof want));
  ck_assert_str_eq(pr.title, "News");
  ck_assert_str_eq(pr.desc, "Evening news");
  ck_assert_str_eq(pr.category, "News");
  bcg_programme_free_text(&pr);
  bim_node_free(n);

  n = bcgfrag_schedule(&f.au, "ch1", in.programmes, ix.first[0], ix.next, 0);
  ck_assert_ptr_nonnull(n);
  ck_assert_int_eq(bcgfrag_read_schedule(n, &out, lookup_title, &calls), 0);
  ck_assert_int_eq(calls, 2);
  ck_assert_int_eq(out.programme_count, 2);
  ck_assert_str_eq(out.programmes[0].channel_id, "ch1");
  ck_assert_str_eq(out.programmes[0].start, "2030-01-01T12:00:00Z");
  ck_assert_str_eq(out.programmes[0].stop, "2030-01-01T13:30:00Z");
  ck_assert_str_eq(out.programmes[0].title, "Looked up");
  ck_assert_str_eq(out.programmes[1].start, "2030-01-01T13:30:00Z");
  ck_assert_str_eq(out.programmes[1].stop, "");
  bim_node_free(n);
  bcg_doc_free(&out);

  n = bcgfrag_service(&f.au, &in.channels[0]);
  ck_assert_ptr_nonnull(n);
  memset(&ch, 0, sizeof ch);
  ck_assert_int_eq(bcgfrag_read_service(n, "dvb://2.1.65", 0, &ch), 0);
  ck_assert_str_eq(ch.uri, "rtp://239.1.1.1:5000");
  ck_assert_int_eq(ch.name_count, 2);
  ck_assert_str_eq(ch.names[0], "Channel One");
  ck_assert_uint_eq(ch.sid, 101u);
  bim_node_free(n);

  bcg_doc_init(&out);
  n = bcgfrag_tva_main(&f.au, "example.com/My%20Guide", "de-AT");
  ck_assert_ptr_nonnull(n);
  ck_assert_int_eq(bcgfrag_read_tva_main(n, &out), 0);
  ck_assert_str_eq(out.publisher, "example.com/My%20Guide");
  ck_assert_str_eq(out.lang, "de-AT");
  bim_node_free(n);
  n = bcgfrag_tva_main(&f.au, NULL, NULL);
  ck_assert_ptr_nonnull(n);
  ck_assert_int_eq(bcgfrag_read_tva_main(n, &out), 0);
  ck_assert_str_eq(out.publisher, "");
  ck_assert_str_eq(out.lang, "");
  bim_node_free(n);

  bcg_doc_free(&out);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_schedule_end_is_the_latest_event_end) {
  bcg_programme_t pr[3];
  int next[3] = {1, 2, -1};
  memset(pr, 0, sizeof pr);
  bufcpy(pr[0].start, sizeof pr[0].start, "2030-01-01T12:00:00Z");
  bufcpy(pr[0].stop, sizeof pr[0].stop, "2030-01-01T14:00:00Z");
  bufcpy(pr[1].start, sizeof pr[1].start, "2030-01-01T13:00:00Z");
  bufcpy(pr[1].stop, sizeof pr[1].stop, "2030-01-01T13:30:00Z");
  bufcpy(pr[2].start, sizeof pr[2].start, "2030-01-01T14:00:00Z");
  ck_assert_str_eq(bcgau_schedule_end(pr, 0, next), "2030-01-01T14:00:00Z");
  ck_assert_str_eq(bcgau_schedule_end(pr, 1, next), "2030-01-01T14:00:00Z");
  ck_assert_str_eq(bcgau_schedule_end(pr, 2, next), "2030-01-01T14:00:00Z");
  ck_assert_ptr_null(bcgau_schedule_end(pr, -1, next));
  bufcpy(pr[1].stop, sizeof pr[1].stop, "2030-01-01T12:30:00Z");
  ck_assert_str_eq(bcgau_schedule_end(pr, 1, next), "2030-01-01T14:00:00Z");
  next[0] = -1;
  ck_assert_str_eq(bcgau_schedule_end(pr, 0, next), "2030-01-01T14:00:00Z");
}
END_TEST

START_TEST(bcgau_frags_write_parse_apply_round_trip) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcgau_frags_t p;
  bcgau_scratch_t sc;
  bcgau_parsed_t pa;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  int ncp = 0;
  fx_open(&f);
  make_doc(&in);
  bcg_doc_init(&out);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &p, &ncp), 0);
  ck_assert_int_eq(ncp, 1);
  ck_assert_uint_eq(p.n, 5);
  ck_assert_int_eq(p.kind[0], BCGAU_PROGRAM);
  ck_assert_int_eq(p.kind[2], BCGAU_SCHEDULE);
  ck_assert_int_eq(p.kind[4], BCGAU_SERVICE);
  ck_assert_int_eq(bcgau_frags_write(&sc, &f.au, &p, &bw, &sw), 0);
  ck_assert_int_eq(sc.refs[0].kind, BCGAU_PROGRAM);
  ck_assert_int_eq(sc.refs[4].kind, BCGAU_SERVICE);
  ck_assert_int_eq(sc.refs[4].index, 1);
  bcgau_frags_free(&p);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(bcgau_parse(&f.au, &br, &sr, NULL, 0, &pa), 0);
  ck_assert_uint_eq(pa.n, 5);
  ck_assert_ptr_nonnull(pa.tails);
  ck_assert_int_eq(bcgau_apply(&f.au, &pa, &out), 0);
  ck_assert_int_eq(out.channel_count, 2);
  ck_assert_str_eq(out.channels[0].names[1], "Kanal Eins");
  ck_assert_uint_eq(out.channels[0].sid, 101u);
  ck_assert_int_eq(out.programme_count, 2);
  ck_assert_ptr_nonnull(find_prog(&out, "Weather"));
  ck_assert_str_eq(out.ns, BCG_FIXTURE_NS);
  bcgau_parsed_free(&pa);
  bcg_doc_free(&out);
  bcg_doc_index_free(&ix);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_xml_parse_xml_apply_round_trip) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcgau_frags_t p;
  bcgau_parsed_t pa;
  char *xml = NULL;
  size_t xl = 0;
  int ncp = 0;
  fx_open(&f);
  make_doc(&in);
  bcg_doc_init(&out);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &p, &ncp), 0);
  ck_assert_int_eq(bcgau_xml(&f.au, p.f, p.n, &xml, &xl), 0);
  bcgau_frags_free(&p);
  ck_assert_ptr_nonnull(strstr(xml, "<TVAMain"));
  ck_assert_ptr_nonnull(strstr(xml, "ProgramInformation"));
  ck_assert_int_eq(bcgau_parse_xml(&f.au, xml, xl, &pa), 0);
  ck_assert_uint_eq(pa.n, 6); /* plus TVAMain with its containers */
  ck_assert_ptr_null(pa.tails);
  ck_assert_int_eq(bcgau_apply(&f.au, &pa, &out), 0);
  ck_assert_int_eq(out.channel_count, 2);
  ck_assert_int_eq(out.programme_count, 2);
  ck_assert_str_eq(find_prog(&out, "News")->desc, "Evening news");
  bcgau_parsed_free(&pa);
  ck_assert_int_ne(bcgau_parse_xml(&f.au, "<a/>", 4, &pa), 0);
  ck_assert_int_ne(bcgau_parse_xml(&f.au, "<TVAMain", 8, &pa), 0);
  free(xml);
  bcg_doc_free(&out);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_frags_tva_main_needs_publisher) {
  fx_t f;
  bcg_doc_t in;
  bcgau_frags_t p;
  fx_open(&f);
  bcg_doc_init(&in);
  ck_assert_int_eq(bcgau_frags_tva_main(&f.au, &in, &p), -1);
  bufcpy(in.publisher, sizeof in.publisher, "example.com/G");
  ck_assert_int_eq(bcgau_frags_tva_main(&f.au, &in, &p), 0);
  ck_assert_uint_eq(p.n, 1);
  ck_assert_int_eq(p.kind[0], BCGAU_TVA_MAIN);
  bcgau_frags_free(&p);
  fx_close(&f);
}
END_TEST

static bim_node_t *tn(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, text) : bim_node_new(name);
  ck_assert_ptr_nonnull(n);
  if (!parent && text) ck_assert_int_eq(bim_node_set_text(n, text), 0);
  return n;
}

START_TEST(bcgfrag_read_program_validates_the_tree) {
  bcg_programme_t pr;
  char crid[8];
  bim_node_t *r;
  bim_node_t *bd;
  bim_node_t *g;
  r = tn(NULL, "ProgramInformation", NULL);
  ck_assert_int_eq(bcgfrag_read_program(r, crid, sizeof crid, &pr), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "programId", "crid://x"), 0);
  ck_assert_int_eq(bcgfrag_read_program(r, crid, sizeof crid, &pr), -1);
  bd = tn(r, "BasicDescription", NULL);
  ck_assert_int_eq(bcgfrag_read_program(r, crid, sizeof crid, &pr), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "programId", "c://x"), 0);
  tn(bd, NULL, "loose");
  tn(bd, "Title", "first");
  tn(bd, "Title", "second");
  tn(bd, "Synopsis", "s1");
  tn(bd, "Synopsis", "s2");
  g = tn(bd, "Genre", NULL);
  tn(bd, "Other", "x");
  ck_assert_int_eq(bcgfrag_read_program(r, crid, sizeof crid, &pr), 0);
  ck_assert_str_eq(pr.title, "first");
  ck_assert_str_eq(pr.desc, "s1");
  ck_assert_ptr_null(pr.category);
  bcg_programme_free_text(&pr);
  tn(g, "Name", "Drama");
  tn(bd, "Genre", NULL);
  ck_assert_int_eq(bcgfrag_read_program(r, crid, sizeof crid, &pr), 0);
  ck_assert_str_eq(pr.category, "Drama");
  bcg_programme_free_text(&pr);
  bim_node_free(r);
}
END_TEST

static int lookup_oom(void *, const char *, bcg_programme_t *) { return -2; }

static int lookup_miss(void *, const char *, bcg_programme_t *) { return -1; }

static bim_node_t *event(bim_node_t *root, const char *crid, const char *start, const char *stop, const char *dur) {
  bim_node_t *ev = tn(root, "ScheduleEvent", NULL);
  bim_node_t *p = tn(ev, "Program", NULL);
  if (crid) ck_assert_int_eq(bim_node_set_attr(p, "crid", crid), 0);
  if (start) tn(ev, "PublishedStartTime", start);
  if (stop) tn(ev, "PublishedEndTime", stop);
  if (dur) tn(ev, "PublishedDuration", dur);
  return ev;
}

START_TEST(bcgfrag_read_schedule_validates_the_tree) {
  bcg_doc_t doc;
  bim_node_t *r = tn(NULL, "Schedule", NULL);
  char longid[BCG_ID_LEN + 8];
  char longtime[BCG_TIME_LEN + 8];
  memset(longid, 'a', sizeof longid - 1);
  longid[sizeof longid - 1] = 0;
  memset(longtime, '1', sizeof longtime - 1);
  longtime[sizeof longtime - 1] = 0;
  bcg_doc_init(&doc);
  ck_assert_int_eq(bcgfrag_read_schedule(r, &doc, NULL, NULL), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "serviceIDRef", longid), 0);
  ck_assert_int_eq(bcgfrag_read_schedule(r, &doc, NULL, NULL), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "serviceIDRef", "ch1"), 0);
  tn(r, NULL, "loose text");
  event(r, NULL, "2030-01-01T12:00:00Z", NULL, NULL);
  event(r, "c1", NULL, NULL, NULL);
  event(r, "c2", longtime, NULL, NULL);
  event(r, "c3", "2030-01-01T12:00:00Z", "2030-01-01T13:00:00Z", NULL);
  event(r, "c4", "2030-01-01T12:00:00Z", longtime, "PT1H30M");
  event(r, "c5", "2030-01-01T12:00:00Z", NULL, "PT45M");
  event(r, "c6", "2030-01-01T12:00:00Z", NULL, "bogus");
  event(r, "c7", "2030-01-01T12:00:00Z", NULL, NULL);
  {
    bim_node_t *ev = tn(r, "ScheduleEvent", NULL);
    tn(ev, "Program", NULL);
    tn(ev, "PublishedStartTime", NULL);
  }
  ck_assert_int_eq(bcgfrag_read_schedule(r, &doc, lookup_miss, NULL), 0);
  ck_assert_int_eq(doc.programme_count, 5);
  ck_assert_str_eq(doc.programmes[0].stop, "2030-01-01T13:00:00Z");
  ck_assert_str_eq(doc.programmes[1].stop, "2030-01-01T13:30:00Z");
  ck_assert_str_eq(doc.programmes[2].stop, "2030-01-01T12:45:00Z");
  ck_assert_str_eq(doc.programmes[3].stop, "");
  ck_assert_str_eq(doc.programmes[4].stop, "");
  bcg_doc_free(&doc);
  bcg_doc_init(&doc);
  ck_assert_int_eq(bcgfrag_read_schedule(r, &doc, lookup_oom, NULL), -1);
  bcg_doc_free(&doc);
  bim_node_free(r);
}
END_TEST

START_TEST(bcgfrag_read_service_validates_the_tree) {
  bcg_channel_t c;
  bim_node_t *r = tn(NULL, "ServiceInformation", NULL);
  char longid[BCG_ID_LEN + 8];
  char longuri[BCG_ID_LEN + 8];
  memset(longid, 'i', sizeof longid - 1);
  longid[sizeof longid - 1] = 0;
  memset(longuri, 'u', sizeof longuri - 1);
  longuri[sizeof longuri - 1] = 0;
  ck_assert_int_eq(bcgfrag_read_service(r, "", 0, &c), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "serviceId", longid), 0);
  ck_assert_int_eq(bcgfrag_read_service(r, "", 0, &c), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "serviceId", "svc"), 0);
  tn(r, "ServiceURL", longuri);
  tn(r, NULL, "loose");
  tn(r, "Other", "x");
  tn(r, "Name", NULL);
  tn(r, "Name", longid);
  for (int i = 0; i < BCG_MAX_NAMES + 2; i++) {
    char nm[16];
    snprintf(nm, sizeof nm, "N%d", i);
    tn(r, "Name", nm);
  }
  ck_assert_int_eq(bcgfrag_read_service(r, NULL, 0, &c), 0);
  ck_assert_str_eq(c.uri, "");
  ck_assert_int_eq(c.name_count, BCG_MAX_NAMES);
  ck_assert_int_eq(bcgfrag_read_service(r, "dvb://2.1.65", 1, &c), 0);
  ck_assert_int_eq(c.name_count, 0);
  ck_assert_uint_eq(c.sid, 101u);
  bim_node_free(r);
}
END_TEST

START_TEST(bcgfrag_read_tva_main_rejects_long_fields) {
  bcg_doc_t doc;
  bim_node_t *r = tn(NULL, "TVAMain", NULL);
  char longpub[BCG_ID_LEN + 8];
  char longlang[BCG_LANG_LEN + 8];
  memset(longpub, 'p', sizeof longpub - 1);
  longpub[sizeof longpub - 1] = 0;
  memset(longlang, 'l', sizeof longlang - 1);
  longlang[sizeof longlang - 1] = 0;
  bcg_doc_init(&doc);
  ck_assert_int_eq(bim_node_set_attr(r, "publisher", longpub), 0);
  ck_assert_int_eq(bcgfrag_read_tva_main(r, &doc), -1);
  ck_assert_int_eq(bim_node_set_attr(r, "publisher", "ok"), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(r, "http://www.w3.org/XML/1998/namespace", "lang", strdup(longlang)), 0);
  ck_assert_int_eq(bcgfrag_read_tva_main(r, &doc), -1);
  bim_node_free(r);
}
END_TEST

START_TEST(bcgfrag_builders_without_optional_parts) {
  fx_t f;
  bcg_programme_t pr;
  bcg_channel_t ch;
  bcg_doc_t in;
  bcg_doc_index_t ix;
  bim_node_t *n;
  fx_open(&f);
  memset(&pr, 0, sizeof pr);
  bufcpy(pr.channel_id, sizeof pr.channel_id, "ch1");
  bufcpy(pr.start, sizeof pr.start, "2030-01-01T12:00:00Z");
  n = bcgfrag_program(&f.au, &pr);
  ck_assert_ptr_nonnull(n);
  bim_node_free(n);
  memset(&ch, 0, sizeof ch);
  bufcpy(ch.id, sizeof ch.id, "svc");
  bufcpy(ch.uri, sizeof ch.uri, "rtp://x");
  n = bcgfrag_service(&f.au, &ch);
  ck_assert_ptr_nonnull(n);
  ck_assert_str_eq(n->kids[0]->text, "svc");
  bim_node_free(n);

  make_doc(&in);
  bufcpy(in.programmes[1].start, sizeof in.programmes[1].start, "bogus time");
  bufcpy(in.programmes[0].stop, sizeof in.programmes[0].stop, "2029-01-01T00:00:00Z");
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  n = bcgfrag_schedule(&f.au, "ch1", in.programmes, ix.first[0], ix.next, 1);
  ck_assert_ptr_nonnull(n);
  bim_node_free(n);
  n = bcgfrag_schedule(&f.au, "ch2", in.programmes, -1, ix.next, 0);
  ck_assert_ptr_nonnull(n);
  bim_node_free(n);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgfrag_builders_allocation_failures) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_index_t ix;
  bcg_programme_t pr;
  bcg_channel_t ch;
  fx_open(&f);
  make_doc(&in);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  for (int what = 0; what < 5; what++)
    for (long n = 0;; n++) {
      bim_node_t *t;
      int hit;
      memset(&pr, 0, sizeof pr);
      alloc_fail_at(n);
      switch (what) {
        case 0: t = bcgfrag_program(&f.au, &in.programmes[0]); break;
        case 1: t = bcgfrag_schedule(&f.au, "ch1", in.programmes, ix.first[0], ix.next, 1); break;
        case 2: t = bcgfrag_service(&f.au, &in.channels[0]); break;
        case 3: t = bcgfrag_tva_main(&f.au, "example.com/g", "de"); break;
        default: {
          bim_node_t *r = NULL;
          alloc_fail_end();
          r = bcgfrag_program(&f.au, &in.programmes[0]);
          alloc_fail_at(n);
          {
            char crid[TVA_CRID_BUF];
            if (!bcgfrag_read_program(r, crid, sizeof crid, &pr)) bcg_programme_free_text(&pr);
          }
          hit = alloc_fail_end();
          bim_node_free(r);
          t = NULL;
          if (!hit) goto next_what;
          continue;
        }
      }
      hit = alloc_fail_end();
      bim_node_free(t);
      if (!hit) break;
    }
next_what:
  memset(&ch, 0, sizeof ch);
  for (long n = 0;; n++) {
    bcg_doc_t out;
    bim_node_t *r = bcgfrag_schedule(&f.au, "ch1", in.programmes, ix.first[0], ix.next, 0);
    int hit;
    bcg_doc_init(&out);
    alloc_fail_at(n);
    (void)bcgfrag_read_schedule(r, &out, NULL, NULL);
    hit = alloc_fail_end();
    bcg_doc_free(&out);
    bim_node_free(r);
    if (!hit) break;
  }
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_survives_allocation_failures) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_index_t ix;
  bcgau_frags_t pf;
  bcgau_scratch_t sc;
  bitwriter_t bw;
  strrepo_writer_t sw;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  unsigned char *abuf;
  unsigned char *sbuf;
  int ncp = 0;
  int nfuu = 0;
  char *xml = NULL;
  size_t xl = 0;
  fx_open(&f);
  make_doc(&in);
  bufcpy(in.publisher, sizeof in.publisher, "example.com/G");
  bufcpy(in.lang, sizeof in.lang, "de");
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &pf, &ncp), 0);
  ck_assert_int_eq(bcgau_frags_write(&sc, &f.au, &pf, &bw, &sw), 0);
  ck_assert_int_eq(bcgau_xml(&f.au, pf.f, pf.n, &xml, &xl), 0);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  abuf = malloc(al);
  sbuf = malloc(sl);
  ck_assert_ptr_nonnull(abuf);
  ck_assert_ptr_nonnull(sbuf);
  memcpy(abuf, a, al);
  memcpy(sbuf, s, sl);
  bcgau_frags_free(&pf);

  for (long n = 0;; n++) {
    int hit;
    alloc_fail_at(n);
    (void)bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, BCGAU_SCHED_TITLES, &pf, &ncp);
    hit = alloc_fail_end();
    if (pf.f) bcgau_frags_free(&pf);
    memset(&pf, 0, sizeof pf);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_scratch_t sc2;
    bitwriter_t bw2;
    strrepo_writer_t sw2;
    int hit;
    ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &pf, &ncp), 0);
    bcgau_scratch_init(&sc2);
    bitwriter_init(&bw2);
    ck_assert_int_eq(strrepo_writer_init(&sw2), 0);
    alloc_fail_at(n);
    (void)bcgau_frags_write(&sc2, &f.au, &pf, &bw2, &sw2);
    hit = alloc_fail_end();
    bcgau_frags_free(&pf);
    bcgau_scratch_free(&sc2);
    bitwriter_free(&bw2);
    strrepo_writer_free(&sw2);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_scratch_t sc2;
    bitwriter_t bw2;
    strrepo_writer_t sw2;
    int hit;
    bcgau_scratch_init(&sc2);
    bitwriter_init(&bw2);
    ck_assert_int_eq(strrepo_writer_init(&sw2), 0);
    alloc_fail_at(n);
    (void)bcgau_encode_tva_main(&sc2, &f.au, &in, &bw2, &sw2);
    hit = alloc_fail_end();
    bcgau_scratch_free(&sc2);
    bitwriter_free(&bw2);
    strrepo_writer_free(&sw2);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_scratch_t sc2;
    bitwriter_t bw2;
    strrepo_writer_t sw2;
    int hit;
    bcgau_scratch_init(&sc2);
    bitwriter_init(&bw2);
    ck_assert_int_eq(strrepo_writer_init(&sw2), 0);
    alloc_fail_at(n);
    (void)bcgau_encode_range(&sc2, &f.au, &in, &ix, 0, in.channel_count, 0, &bw2, &sw2, &nfuu, NULL);
    hit = alloc_fail_end();
    bcgau_scratch_free(&sc2);
    bitwriter_free(&bw2);
    strrepo_writer_free(&sw2);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_parsed_t pa;
    bitreader_t br;
    strrepo_reader_t sr;
    int hit;
    bitreader_init(&br, abuf, al);
    ck_assert_int_eq(strrepo_reader_init(&sr, sbuf, sl), 0);
    memset(&pa, 0, sizeof pa);
    alloc_fail_at(n);
    (void)bcgau_parse(&f.au, &br, &sr, NULL, 0, &pa);
    hit = alloc_fail_end();
    bcgau_parsed_free(&pa);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_parsed_t pa;
    bcg_doc_t out;
    bitreader_t br;
    strrepo_reader_t sr;
    int hit;
    bitreader_init(&br, abuf, al);
    ck_assert_int_eq(strrepo_reader_init(&sr, sbuf, sl), 0);
    ck_assert_int_eq(bcgau_parse(&f.au, &br, &sr, NULL, 0, &pa), 0);
    bcg_doc_init(&out);
    alloc_fail_at(n);
    (void)bcgau_apply(&f.au, &pa, &out);
    hit = alloc_fail_end();
    bcg_doc_free(&out);
    bcgau_parsed_free(&pa);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcgau_parsed_t pa;
    int hit;
    memset(&pa, 0, sizeof pa);
    alloc_fail_at(n);
    (void)bcgau_parse_xml(&f.au, xml, xl, &pa);
    hit = alloc_fail_end();
    bcgau_parsed_free(&pa);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    char *x2 = NULL;
    size_t l2 = 0;
    int hit;
    ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &pf, &ncp), 0);
    alloc_fail_at(n);
    (void)bcgau_xml(&f.au, pf.f, pf.n, &x2, &l2);
    hit = alloc_fail_end();
    free(x2);
    bcgau_frags_free(&pf);
    if (!hit) break;
  }

  for (long n = 0;; n++) {
    bcg_doc_t out;
    bitreader_t br;
    strrepo_reader_t sr;
    int hit;
    int nd = 0;
    bitreader_init(&br, abuf, al);
    ck_assert_int_eq(strrepo_reader_init(&sr, sbuf, sl), 0);
    bcg_doc_init(&out);
    alloc_fail_at(n);
    (void)bcgau_decode_refs(&f.au, &br, &sr, NULL, 0, &out, &nd);
    hit = alloc_fail_end();
    bcg_doc_free(&out);
    if (!hit) break;
  }
  free(xml);
  free(abuf);
  free(sbuf);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

START_TEST(bcgau_range_edges_and_tail_fallbacks) {
  fx_t f;
  bcg_doc_t in;
  bcg_doc_t out;
  bcg_doc_index_t ix;
  bcgau_frags_t pf;
  bcgau_scratch_t sc;
  bcgau_parsed_t pa;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *s;
  size_t al;
  size_t sl;
  int ncp = 0;
  bcg_doc_init(&out);
  fx_open(&f);
  make_doc(&in);
  in.channels[1].uri[0] = 0;
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, -3, in.channel_count + 5, BCGAU_SCHED_TITLES, &pf, &ncp), 0);
  ck_assert_uint_eq(pf.n, 4);
  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bcgau_write(&sc, &f.au, pf.f, pf.n, NULL, &bw, &sw), 0);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  (void)bcgau_parse(&f.au, &br, &sr, NULL, 0, &pa);
  bcgau_parsed_free(&pa);
  bcgau_frags_free(&pf);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);

  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 1, 2, 0, &pf, NULL), 0);
  ck_assert_uint_eq(pf.n, 0);
  bcgau_frags_free(&pf);

  bcgau_scratch_init(&sc);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  bcg_doc_index_free(&ix);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &in), 0);
  ck_assert_int_eq(bcgau_frags_range(&f.au, &in, &ix, 0, in.channel_count, 0, &pf, &ncp), 0);
  ck_assert_int_eq(bcgau_frags_write(&sc, &f.au, &pf, &bw, &sw), 0);
  a = bitwriter_data(&bw, &al);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(bcgau_parse(&f.au, &br, &sr, NULL, 0, &pa), 0);
  for (size_t i = 0; i < pa.n; i++)
    if (pa.f[i].code == 0x0001) pa.f[i].code = 0x7777;
  ck_assert_int_eq(bcgau_apply(&f.au, &pa, &out), 0);
  for (size_t i = 0; i < pa.n; i++) {
    bim_node_free(pa.f[i].root);
    pa.f[i].root = bim_node_new("Bogus");
    ck_assert_ptr_nonnull(pa.f[i].root);
    if (pa.f[i].code == 0x7777) pa.f[i].code = 0x0001;
  }
  for (size_t i = 0; i < pa.n; i++) {
    bcg_doc_t d2;
    bcgau_parsed_t one = {&pa.f[i], 1, NULL};
    bcg_doc_init(&d2);
    ck_assert_int_eq(bcgau_apply(&f.au, &one, &d2), -1);
    bcg_doc_free(&d2);
  }
  bcgau_parsed_free(&pa);
  bcgau_frags_free(&pf);
  bcgau_scratch_free(&sc);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  bcg_doc_free(&out);
  bcg_doc_index_free(&ix);
  bcg_doc_free(&in);
  fx_close(&f);
}
END_TEST

static Suite *bcgau_suite(void) {
  Suite *s = suite_create("bim_bcgau");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bcgau_range_round_trips_through_the_schema);
  tcase_add_test(tc, bcgau_tva_main_carries_publisher_and_lang);
  tcase_add_test(tc, bcgau_decode_rejects_truncated_access_unit);
  tcase_add_test(tc, bcgfrag_builders_and_readers_round_trip);
  tcase_add_test(tc, bcgau_schedule_end_is_the_latest_event_end);
  tcase_add_test(tc, bcgau_frags_write_parse_apply_round_trip);
  tcase_add_test(tc, bcgau_xml_parse_xml_apply_round_trip);
  tcase_add_test(tc, bcgau_frags_tva_main_needs_publisher);
  tcase_add_test(tc, bcgfrag_read_program_validates_the_tree);
  tcase_add_test(tc, bcgfrag_read_schedule_validates_the_tree);
  tcase_add_test(tc, bcgfrag_read_service_validates_the_tree);
  tcase_add_test(tc, bcgfrag_read_tva_main_rejects_long_fields);
  tcase_add_test(tc, bcgfrag_builders_without_optional_parts);
  tcase_add_test(tc, bcgfrag_builders_allocation_failures);
  tcase_add_test(tc, bcgau_survives_allocation_failures);
  tcase_add_test(tc, bcgau_range_edges_and_tail_fallbacks);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bcgau_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
