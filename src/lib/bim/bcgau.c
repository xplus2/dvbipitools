/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/bcgfrag.h"
#include "lib/bim/nodexml.h"
#include "lib/sys/ioutil.h"

#define CODE_PROGRAM 0x0001
#define CODE_SCHEDULE 0x0005
#define CODE_SERVICE 0x0006
#define CODE_TVA_MAIN 0x000E

void bcgau_scratch_init(bcgau_scratch_t *sc) { memset(sc, 0, sizeof *sc); }

void bcgau_scratch_free(bcgau_scratch_t *sc) {
  free(sc->refs);
  memset(sc, 0, sizeof *sc);
}

void bcgau_init(bcgau_t *a, const xsd_compiled_t *c, const char *tva_ns, const bim_encode_opts_t *o) {
  memset(a, 0, sizeof *a);
  a->c = c;
  a->tva_ns = tva_ns;
  a->ns.tva = tva_ns;
  bim_tvans_detect(c, &a->ns);
  if (o) {
    a->eo.dvb_locator = o->dvb_locator;
    a->eo.dvb_duration = o->dvb_duration;
    a->eo.dvb_pubtime = o->dvb_pubtime;
    a->eo.cs = o->cs;
    a->dop.cs = o->cs;
  }
  a->dop.schemas = &c->schema;
  a->eo.schemas = a->dop.schemas;
  a->dop.nschemas = 1;
  a->eo.nschemas = a->dop.nschemas;
  a->dop.lax_schema = -1;
  a->eo.lax_schema = a->dop.lax_schema;
  a->eo.frag_format = 1;
}

void bcgau_frags_free(bcgau_frags_t *p) {
  for (size_t i = 0; i < p->n; i++) bim_node_free(p->f[i].root);
  free(p->f);
  free(p->kind);
  free(p->index);
  free(p->chan);
  memset(p, 0, sizeof *p);
}

static int frags_init(bcgau_frags_t *p, size_t cap) {
  memset(p, 0, sizeof *p);
  p->cap = cap;
  p->f = calloc(cap + 1, sizeof *p->f);
  p->kind = calloc(cap + 1, sizeof *p->kind);
  p->index = calloc(cap + 1, sizeof *p->index);
  p->chan = calloc(cap + 1, sizeof *p->chan);
  if (p->f && p->kind && p->index && p->chan) return 0;
  bcgau_frags_free(p);
  return -1;
}

/* takes root */
static int frags_add(bcgau_frags_t *p, unsigned code, bim_node_t *root, int kind, int index, const bcg_channel_t *chan) {
  if (!root) return -1;
  if (p->n >= p->cap) {
    bim_node_free(root);
    return -1;
  }
  p->f[p->n] = (dvbtva_fuu_t){code, root};
  p->kind[p->n] = (uint8_t)kind;
  p->index[p->n] = index;
  p->chan[p->n] = chan;
  p->n++;
  return 0;
}

int bcgau_frags_range(const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, int ch_from, int ch_to, unsigned flags, bcgau_frags_t *p, int *nchan_prog) {
  size_t n = 0;
  int ncp = 0;
  if (ch_from < 0) ch_from = 0;
  if (ch_to > doc->channel_count) ch_to = doc->channel_count;
  for (int i = ch_from; i < ch_to; i++) {
    ncp += ix->nprog[i] > 0;
    if (doc->channels[i].uri[0]) n += (size_t)ix->nprog[i] + (ix->nprog[i] > 0) + 1;
  }
  if (frags_init(p, n)) return -1;
  for (int i = ch_from; i < ch_to; i++)
    for (int j = ix->first[i]; doc->channels[i].uri[0] && j >= 0; j = ix->next[j])
      if (frags_add(p, CODE_PROGRAM, bcgfrag_program(a, &doc->programmes[j]), BCGAU_PROGRAM, j, NULL)) goto fail;
  for (int i = ch_from; i < ch_to; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (c->uri[0] && ix->nprog[i] &&
        frags_add(p, CODE_SCHEDULE, bcgfrag_schedule(a, c->id, doc->programmes, ix->first[i], ix->next, (flags & BCGAU_SCHED_TITLES) != 0), BCGAU_SCHEDULE, i, NULL))
      goto fail;
  }
  for (int i = ch_from; i < ch_to; i++) {
    const bcg_channel_t *c = &doc->channels[i];
    if (c->uri[0] && frags_add(p, CODE_SERVICE, bcgfrag_service(a, c), BCGAU_SERVICE, i, c)) goto fail;
  }
  if (nchan_prog) *nchan_prog = ncp;
  return 0;
fail:
  bcgau_frags_free(p);
  return -1;
}

int bcgau_frags_tva_main(const bcgau_t *a, const bcg_doc_t *doc, bcgau_frags_t *p) {
  if (!doc->publisher[0] || frags_init(p, 1)) return -1;
  if (frags_add(p, CODE_TVA_MAIN, bcgfrag_tva_main(a, doc->publisher, doc->lang[0] ? doc->lang : NULL), BCGAU_TVA_MAIN, 0, NULL)) {
    bcgau_frags_free(p);
    return -1;
  }
  return 0;
}

/* the channel's dvb:// triplet ("" if none) and "1" if Name was made of the id follow a service fragment's strings,
   receivers find strings by fragment reference and never see them */
static int service_tail(void *ctx, size_t i, strrepo_writer_t *sw) {
  const bcg_channel_t *c = ((const bcg_channel_t *const *)ctx)[i];
  char dtt[sizeof ((bcgau_tail_t *)0)->dtt];
  if (!c) return 0;
  bcg_channel_dtt_format(c, dtt, sizeof dtt);
  return dvb_string_encode(sw, dtt) || dvb_string_encode(sw, c->name_count == 0 ? "1" : "0");
}

int bcgau_write(bcgau_scratch_t *sc, const bcgau_t *a, const dvbtva_fuu_t *f, size_t n, const bcg_channel_t *const *chan, bitwriter_t *bw, strrepo_writer_t *sw) {
  dvbtva_roots_t r = {a->c, &a->ns};
  bim_fragref_t *br = calloc(n + 1, sizeof *br);
  bim_err_t err;
  int rc = -1;
  if (br && !array_reserve((void **)&sc->refs, &sc->refs_cap, (int)n, sizeof *sc->refs) &&
      !dvbtva_au_write_ex(dvbtva_tva_root, &r, &a->eo, f, n, bw, sw, br, chan ? &service_tail : NULL, (void *)chan, &err)) {
    for (size_t i = 0; i < n; i++) sc->refs[i] = (bcgau_ref_t){.au_off = br[i].au_off, .str_off = br[i].str_off, .kind = BCGAU_OTHER};
    rc = bitwriter_flush(bw);
  }
  free(br);
  return rc;
}

int bcgau_frags_write(bcgau_scratch_t *sc, const bcgau_t *a, const bcgau_frags_t *p, bitwriter_t *bw, strrepo_writer_t *sw) {
  if (bcgau_write(sc, a, p->f, p->n, p->chan, bw, sw)) return -1;
  for (size_t i = 0; i < p->n; i++) {
    sc->refs[i].kind = p->kind[i];
    sc->refs[i].index = p->index[i];
  }
  return 0;
}

int bcgau_xml(const bcgau_t *a, dvbtva_fuu_t *f, size_t n, char **xml, size_t *len) {
  bim_node_t *doc;
  bim_xml_opts_t xo = {.indent = 2, .declaration = 1};
  int rc = bim_tvafrag_join(a->c, &a->ns, f, n, &doc);
  if (rc) return -1;
  rc = bim_node_to_xml(doc, &xo, xml, len);
  bim_node_free(doc);
  return rc;
}

int bcgau_encode_range(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, int ch_from, int ch_to, unsigned flags, bitwriter_t *bw,
                       strrepo_writer_t *sw, int *out_nfuu, int *out_nchan_prog) {
  bcgau_frags_t p;
  int rc = bcgau_frags_range(a, doc, ix, ch_from, ch_to, flags, &p, out_nchan_prog);
  if (rc) return -1;
  rc = bcgau_frags_write(sc, a, &p, bw, sw);
  *out_nfuu = (int)p.n;
  bcgau_frags_free(&p);
  return rc;
}

int bcgau_encode_tva_main(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, bitwriter_t *bw, strrepo_writer_t *sw) {
  bcgau_frags_t p;
  int rc = bcgau_frags_tva_main(a, doc, &p);
  if (rc) return -1;
  rc = bcgau_frags_write(sc, a, &p, bw, sw);
  bcgau_frags_free(&p);
  return rc;
}

void bcgau_parsed_free(bcgau_parsed_t *p) {
  dvbtva_au_free(p->f, p->n);
  free(p->tails);
  memset(p, 0, sizeof *p);
}

typedef struct {
  bcgau_tail_t *v;
  size_t cap;
} tails_t;

/* 2 strings of service_tail when plausible, else belong to next fragment */
static int read_tail(void *ctx, size_t k, const dvbtva_fuu_t *f, strrepo_reader_t *sr) {
  tails_t *t = ctx;
  size_t pos = sr->pos;
  char synth[8];
  bcgau_tail_t tail = {"", 0};
  if (k >= t->cap) {
    size_t cap = t->cap ? t->cap * 2 : 64;
    bcgau_tail_t *np = realloc(t->v, cap * sizeof *np);
    if (!np) return -1;
    t->v = np;
    t->cap = cap;
  }
  if (f->code == CODE_SERVICE) {
    if (!dvb_string_decode(sr, tail.dtt, sizeof tail.dtt) && !dvb_string_decode(sr, synth, sizeof synth) && (!strcmp(synth, "0") || !strcmp(synth, "1")) &&
        (!tail.dtt[0] || !strncmp(tail.dtt, "dvb://", 6))) {
      tail.no_names = synth[0] == '1';
    } else {
      sr->pos = pos;
      tail = (bcgau_tail_t){"", 0};
    }
  }
  t->v[k] = tail;
  return 0;
}

int bcgau_parse(const bcgau_t *a, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, int nrefs, bcgau_parsed_t *out) {
  dvbtva_roots_t r = {a->c, &a->ns};
  tails_t t = {0};
  bim_err_t err;
  memset(out, 0, sizeof *out);
  if (dvbtva_au_parse_ex(dvbtva_tva_root, &r, &a->dop, br, sr, refs, (size_t)nrefs, read_tail, &t, &out->f, &out->n, &err)) {
    free(t.v);
    return -1;
  }
  out->tails = t.v;
  return 0;
}

int bcgau_parse_xml(const bcgau_t *a, const char *xml, size_t len, bcgau_parsed_t *out) {
  xml_doc_t *x = NULL;
  int rc = -1;
  memset(out, 0, sizeof *out);
  if (xml_doc_parse(xml, len, &x, NULL)) return -1;
  if (!bim_tvafrag_split(a->c, &a->ns, xml_doc_root(x), &out->f, &out->n)) rc = 0;
  xml_doc_free(x);
  return rc;
}

typedef struct {
  bcg_progtext_t *arr;
  int n;
  int cap;
  bcg_key_idx_t *idx;
} ptext_t;

static int ptext_add(ptext_t *pt, const char *crid, bcg_programme_t *tmp) {
  bcg_progtext_t *e;
  if (pt->n >= pt->cap) {
    int cap = pt->cap ? pt->cap * 2 : 64;
    bcg_progtext_t *np = realloc(pt->arr, sizeof *np * (size_t)cap);
    if (!np) return -1;
    pt->arr = np;
    pt->cap = cap;
  }
  e = &pt->arr[pt->n];
  if (!(e->crid = strdup(crid))) return -1;
  e->title = tmp->title;
  e->desc = tmp->desc;
  e->category = tmp->category;
  tmp->category = NULL;
  tmp->desc = tmp->category;
  tmp->title = tmp->desc;
  pt->n++;
  return 0;
}

static int ptext_lookup(void *ctx, const char *crid, bcg_programme_t *pr) {
  ptext_t *pt = ctx;
  int i = -1;
  if (!pt->idx && pt->n > 0) {
    pt->idx = malloc(sizeof *pt->idx * (size_t)pt->n);
    if (pt->idx) {
      for (int k = 0; k < pt->n; k++) pt->idx[k] = (bcg_key_idx_t){pt->arr[k].crid, k};
      bcg_key_index_sort(pt->idx, pt->n);
    }
  }
  if (pt->idx) i = bcg_key_index_find(pt->idx, pt->n, crid);
  for (int k = 0; !pt->idx && k < pt->n && i < 0; k++)
    if (!strcmp(pt->arr[k].crid, crid)) i = k;
  if (i < 0) return -1;
  return bcg_progtext_apply(pr, &pt->arr[i]) ? -2 : 0;
}

/* schedules need the programme texts, services follow. -1 not handled */
static int phase(unsigned code) {
  switch (code) {
    case CODE_TVA_MAIN:
    case CODE_PROGRAM: return 0;
    case CODE_SCHEDULE: return 1;
    case CODE_SERVICE: return 2;
    default: return -1;
  }
}

static int apply_one(const dvbtva_fuu_t *f, const bcgau_tail_t *tail, bcg_doc_t *doc, ptext_t *pt) {
  switch (f->code) {
    case CODE_PROGRAM: {
      bcg_programme_t tmp;
      char crid[BCG_ID_LEN * 3 + 64];
      if (bcgfrag_read_program(f->root, crid, sizeof crid, &tmp)) return -1;
      if (ptext_add(pt, crid, &tmp)) {
        bcg_programme_free_text(&tmp);
        return -1;
      }
      return 0;
    }
    case CODE_SCHEDULE: return bcgfrag_read_schedule(f->root, doc, ptext_lookup, pt);
    case CODE_TVA_MAIN: return bcgfrag_read_tva_main(f->root, doc);
    default: {
      bcg_channel_t *c = bcg_add_channel(doc);
      if (!c) return -1;
      return bcgfrag_read_service(f->root, tail ? tail->dtt : "", tail && tail->no_names, c);
    }
  }
}

int bcgau_apply(const bcgau_t *a, const bcgau_parsed_t *p, bcg_doc_t *doc) {
  ptext_t pt = {0};
  int rc = -1;
  bufcpy(doc->ns, sizeof doc->ns, a->tva_ns);
  for (int pass = 0; pass < 3; pass++)
    for (size_t i = 0; i < p->n; i++)
      if (phase(p->f[i].code) == pass && apply_one(&p->f[i], p->tails ? &p->tails[i] : NULL, doc, &pt)) goto done;
  rc = 0;
done:
  for (int i = 0; i < pt.n; i++) bcg_progtext_free(&pt.arr[i]);
  free(pt.arr);
  free(pt.idx);
  return rc;
}

int bcgau_decode_refs(const bcgau_t *a, bitreader_t *br, strrepo_reader_t *sr, const bim_fragref_t *refs, int nrefs, bcg_doc_t *doc, int *out_nfuu) {
  bcgau_parsed_t p;
  int rc = bcgau_parse(a, br, sr, refs, nrefs, &p);
  if (rc) return -1;
  rc = bcgau_apply(a, &p, doc);
  *out_nfuu = (int)p.n;
  bcgau_parsed_free(&p);
  return rc;
}
