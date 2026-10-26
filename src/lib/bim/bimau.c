/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/bimau.h"

#define E_TRUNC "LE400299"
#define E_RANGE "LE400300"
#define E_FIRST "LE400301"
#define E_SCHEMA "LE400302"

typedef enum { T_TYPES, T_ELEMS, T_ATTRS } table_kind_t;

typedef struct {
  bim_rd_t r;
  bim_au_state_t *st;
} rd_t;

static int range(bim_err_t *err, const char *what) { return bim_fail(err, E_RANGE, "bim: access unit %s out of range", what); }

static void *alloc_n(size_t n, size_t sz) { return n ? calloc(n, sz) : NULL; }

static int rd_str(rd_t *r, char **out) {
  uint64_t n;
  if (bim_rd_v8(&r->r, &n)) return -1;
  return n ? bim_rd_bytes(&r->r, n, (unsigned char **)out) : range(r->r.err, "string length");
}

static int w_str(bim_wr_t *w, const char *s) { return s && *s ? bim_wr_str(w, NULL, NULL, s) : range(w->err, "string length"); }

/* 6.7.5.2, 6.7.6.2, 6.7.7.2 (Cor 2) */
static int rd_table(rd_t *r, table_kind_t k, int first, uint64_t known, bim_suu_table_t *t) {
  uint64_t v;
  int aw;
  t->nglobal = known;
  if (first && (bim_rd_v8(&r->r, &t->nglobal) || (k != T_ATTRS && bim_rd_ext(&r->r, t->nglobal, E_RANGE, &t->ext)))) return -1;
  if (bim_rd_flag(&r->r, &t->partial)) return -1;
  if (k == T_ATTRS) {
    if (!t->partial) return 0;
    aw = bim_width(t->nglobal);
    if (bim_rd_v5(&r->r, &v)) return -1;
    if (v > bitreader_bits_left(&r->r.br) / (uint64_t)(aw ? aw : 1)) return bim_rd_trunc(&r->r);
    t->n = (size_t)v;
    if (t->n && !(t->codes = alloc_n(t->n, sizeof *t->codes))) return bim_oom(r->r.err);
    for (size_t i = 0; i < t->n; i++) {
      if (bim_rd_bits(&r->r, aw, &t->codes[i])) return -1;
      if (t->codes[i] >= t->nglobal) return range(r->r.err, "TransmittedAttribute");
    }
    return 0;
  }
  if (t->partial ? bim_rd_v5(&r->r, &v) : (v = t->nglobal, 0)) return -1;
  if (v > bitreader_bits_left(&r->r.br) / (t->partial ? 10 : 5)) return bim_rd_trunc(&r->r);
  t->n = (size_t)v;
  if (t->n && (!(t->counts = alloc_n(t->n, sizeof *t->counts)) || (t->partial && !(t->codes = alloc_n(t->n, sizeof *t->codes))))) return bim_oom(r->r.err);
  for (size_t i = 0; i < t->n; i++) {
    if (t->partial) {
      if (bim_rd_v5(&r->r, &t->codes[i])) return -1;
      if (t->codes[i] >= t->nglobal) return range(r->r.err, k == T_TYPES ? "TransmittedType" : "TransmittedElement");
    }
    if (bim_rd_v5(&r->r, &t->counts[i])) return -1;
  }
  return 0;
}

/* 6.7.8.2.2: one access unit, one FUU */
static int rd_encoded(rd_t *r, bim_bits_t *b) {
  bitreader_t start = r->r.br;
  size_t from = bim_rd_pos(&r->r);
  uint64_t n;
  uint64_t len;
  if (bim_rd_v8(&r->r, &n)) return -1;
  if (n != 1) return bim_fail(r->r.err, E_SCHEMA, "bim: binary encoded schema %s", "not one FUU");
  if (bim_rd_v8(&r->r, &len)) return -1;
  if (len > bitreader_bits_left(&r->r.br) / 8 || bitreader_skip(&r->r.br, (size_t)len * 8)) return bim_rd_trunc(&r->r);
  {
    bim_rd_t back = r->r;
    back.br = start;
    return bim_rd_raw(&back, bim_rd_pos(&r->r) - from, b);
  }
}

static int ns_taken(const bim_au_state_t *st, size_t self, const char *ns) {
  for (size_t i = 0; i < st->di->nschemas; i++)
    if (!strcmp(st->di->schemas[i].uri, ns)) return 1;
  for (size_t i = 0; i < st->nadd; i++)
    if (i != self && st->add[i].ns && !strcmp(st->add[i].ns, ns)) return 1;
  return 0;
}

/* 6.2.3: index 0 no-namespace attributes, last ISO reserved */
static int schema_ok(const bim_au_state_t *st, uint64_t s) { return s && s + 1 < st->nadd; }

static int first_missing(bim_err_t *err, uint64_t s) { return bim_fail(err, E_FIRST, "bim: SUU for schema %llu before its first SUU", (unsigned long long)s); }

static int remember(bim_au_state_t *st, const bim_suu_t *s, bim_err_t *err) {
  bim_au_schema_t *a = &st->add[s->schema];
  /* 6.7.1: definitions never change, one schema ID per namespace */
  if (a->ns) {
    if (strcmp(a->ns, s->ns) || a->ntypes != s->types.nglobal || a->nelems != s->elems.nglobal || a->nattrs != s->attrs.nglobal)
      return range(err, "repeated first SUU");
    return 0;
  }
  if (ns_taken(st, (size_t)s->schema, s->ns)) return range(err, "NamespaceURI");
  if (!(a->ns = strdup(s->ns))) return bim_oom(err);
  a->ntypes = s->types.nglobal;
  a->nelems = s->elems.nglobal;
  a->nattrs = s->attrs.nglobal;
  return 0;
}

static int rd_suu(rd_t *r, bim_suu_t *s) {
  bim_au_state_t *st = r->st;
  bim_au_schema_t *a;
  uint64_t v;
  if (bim_rd_bits(&r->r, bim_width(st->nadd), &s->schema)) return -1;
  if (!schema_ok(st, s->schema)) return range(r->r.err, "SchemaToUpdate");
  a = &st->add[s->schema];
  if (bim_rd_flag(&r->r, &s->first)) return -1;
  if (s->first) {
    if (rd_str(r, &s->ns) || bim_rd_v8(&r->r, &v)) return -1;
    if (v > bitreader_bits_left(&r->r.br) / 16) return bim_rd_trunc(&r->r);
    s->nimported = (size_t)v;
    if (s->nimported && !(s->imported = alloc_n(s->nimported, sizeof *s->imported))) {
      s->nimported = 0;
      return bim_oom(r->r.err);
    }
    for (size_t i = 0; i < s->nimported; i++)
      if (rd_str(r, &s->imported[i])) return -1;
  } else if (!a->ns) {
    return first_missing(r->r.err, s->schema);
  }
  if (rd_table(r, T_TYPES, s->first, a->ntypes, &s->types) || rd_table(r, T_ELEMS, s->first, a->nelems, &s->elems) ||
      rd_table(r, T_ATTRS, s->first, a->nattrs, &s->attrs) || rd_encoded(r, &s->encoded))
    return -1;
  return s->first ? remember(st, s, r->r.err) : 0;
}

static int rd_au(rd_t *r, bim_au_t *au) {
  const bim_decoderinit_t *di = r->st->di;
  if (di->add_schema) {
    if (bim_rd_count(&r->r, 8, &au->nsuus)) return -1;
    if (au->nsuus && !(au->suus = alloc_n(au->nsuus, sizeof *au->suus))) {
      au->nsuus = 0;
      return bim_oom(r->r.err);
    }
    for (size_t i = 0; i < au->nsuus; i++)
      if (rd_suu(r, &au->suus[i])) return -1;
  }
  if (di->add_schema_only) return 0;
  if (bim_rd_count(&r->r, 8, &au->nfuus)) return -1;
  if (au->nfuus && !(au->fuus = alloc_n(au->nfuus, sizeof *au->fuus))) {
    au->nfuus = 0;
    return bim_oom(r->r.err);
  }
  for (size_t i = 0; i < au->nfuus; i++) {
    uint64_t v;
    if (bim_rd_v8(&r->r, &v)) return -1;
    if (v > bitreader_bits_left(&r->r.br) / 8) return bim_rd_trunc(&r->r);
    if (bim_rd_raw(&r->r, v * 8, &au->fuus[i])) return -1;
  }
  return 0;
}

int bim_au_state_init(bim_au_state_t *st, const bim_decoderinit_t *di) {
  st->di = di;
  st->nadd = di->add_schema ? (size_t)di->nadditional : 0;
  st->add = alloc_n(st->nadd, sizeof *st->add);
  if (st->nadd && !st->add) {
    st->nadd = 0;
    return -1;
  }
  return 0;
}

void bim_au_state_free(bim_au_state_t *st) {
  for (size_t i = 0; i < st->nadd; i++) free(st->add[i].ns);
  free(st->add);
  memset(st, 0, sizeof *st);
}

int bim_au_parse(bim_au_state_t *st, const unsigned char *buf, size_t len, bim_au_t *au, size_t *used, bim_err_t *err) {
  rd_t r = {{.err = err, .trunc = E_TRUNC, .what = "access unit"}, st};
  memset(au, 0, sizeof *au);
  memset(err, 0, sizeof *err);
  bitreader_init(&r.r.br, buf, len);
  if (rd_au(&r, au)) {
    bim_au_free(au);
    return -1;
  }
  *used = (bim_rd_pos(&r.r) + 7) / 8;
  return 0;
}

static void free_table(bim_suu_table_t *t) {
  bim_ext_free(&t->ext);
  free(t->codes);
  free(t->counts);
}

void bim_au_free(bim_au_t *au) {
  for (size_t i = 0; i < au->nsuus; i++) {
    bim_suu_t *s = &au->suus[i];
    free(s->ns);
    for (size_t j = 0; j < s->nimported; j++) free(s->imported[j]);
    free(s->imported);
    free_table(&s->types);
    free_table(&s->elems);
    free_table(&s->attrs);
    free(s->encoded.bits);
  }
  for (size_t i = 0; i < au->nfuus; i++) free(au->fuus[i].bits);
  free(au->suus);
  free(au->fuus);
  memset(au, 0, sizeof *au);
}

static int w_table(bim_wr_t *w, table_kind_t k, int first, uint64_t known, const bim_suu_table_t *t) {
  uint64_t nglobal = first ? t->nglobal : known;
  if (first && (bim_wr_v8(w, t->nglobal) || (k != T_ATTRS && bim_wr_ext(w, t->nglobal, E_RANGE, &t->ext)))) return -1;
  if (bim_wr_put(w, (uint64_t)!!t->partial, 1)) return -1;
  if (k == T_ATTRS && !t->partial) return 0;
  if (!t->partial && t->n != nglobal) return range(w->err, "NumOfSubtypes count");
  if (t->partial && bim_wr_v5(w, t->n)) return -1;
  for (size_t i = 0; i < t->n; i++) {
    if (t->partial) {
      if (t->codes[i] >= nglobal) return range(w->err, k == T_ATTRS ? "TransmittedAttribute" : k == T_TYPES ? "TransmittedType" : "TransmittedElement");
      if (k == T_ATTRS ? bim_wr_put(w, t->codes[i], bim_width(nglobal)) : bim_wr_v5(w, t->codes[i])) return -1;
    }
    if (k != T_ATTRS && bim_wr_v5(w, t->counts[i])) return -1;
  }
  return 0;
}

static int w_suu(bim_wr_t *w, bim_au_state_t *st, const bim_suu_t *s) {
  bim_au_schema_t *a;
  if (!schema_ok(st, s->schema)) return range(w->err, "SchemaToUpdate");
  a = &st->add[s->schema];
  if (!s->first && !a->ns) return first_missing(w->err, s->schema);
  if (bim_wr_put(w, s->schema, bim_width(st->nadd)) || bim_wr_put(w, (uint64_t)!!s->first, 1)) return -1;
  if (s->first) {
    if (w_str(w, s->ns) || bim_wr_v8(w, s->nimported)) return -1;
    for (size_t i = 0; i < s->nimported; i++)
      if (w_str(w, s->imported[i])) return -1;
  }
  if (w_table(w, T_TYPES, s->first, a->ntypes, &s->types) || w_table(w, T_ELEMS, s->first, a->nelems, &s->elems) ||
      w_table(w, T_ATTRS, s->first, a->nattrs, &s->attrs) || bim_wr_bits(w, &s->encoded))
    return -1;
  return s->first ? remember(st, s, w->err) : 0;
}

int bim_au_write(bim_au_state_t *st, const bim_au_t *au, bitwriter_t *bw, bim_err_t *err) {
  bim_wr_t w = {bw, err};
  memset(err, 0, sizeof *err);
  if (!st->di->add_schema && au->nsuus) return range(err, "SUU without AdditionalSchemaFlag");
  if (st->di->add_schema_only && au->nfuus) return range(err, "FUU with AdditionalSchemaUpdatesOnlyFlag");
  if (st->di->add_schema) {
    if (bim_wr_v8(&w, au->nsuus)) return -1;
    for (size_t i = 0; i < au->nsuus; i++)
      if (w_suu(&w, st, &au->suus[i])) return -1;
  }
  if (!st->di->add_schema_only) {
    if (bim_wr_v8(&w, au->nfuus)) return -1;
    for (size_t i = 0; i < au->nfuus; i++) {
      if (au->fuus[i].nbits % 8) return range(err, "FUU length");
      if (bim_wr_v8(&w, au->fuus[i].nbits / 8) || bim_wr_bits(&w, &au->fuus[i])) return -1;
    }
  }
  return bitwriter_flush(bw) ? bim_oom(err) : 0;
}
