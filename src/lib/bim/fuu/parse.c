/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/fuu/priv.h"

/* SchemaSwitching branch: SchemaID + global code */
static int rd_global(fr_t *r, int elems, const void **x) {
  uint64_t id;
  uint64_t v;
  int n;
  if (bim_rd_bits(&r->r, bim_width(bim_di_nall(r->st->di)), &id)) return -1;
  if (id >= r->st->nschemas) return range(r, "SchemaID");
  n = elems ? r->st->schemas[id].nelems : r->st->schemas[id].ntypes;
  if (bim_rd_bits(&r->r, bim_width((uint64_t)n), &v)) return -1;
  if (v >= (uint64_t)n) return range(r, elems ? "GlobalSubstitutionSelect" : "GlobalTypeIdentificationCode");
  *x = elems ? (const void *)r->st->schemas[id].elems[v] : (const void *)r->st->schemas[id].types[v];
  return 0;
}

/* 6.6.5.4 */
static int rd_type_code(fr_t *r, const bim_type_t *t, const bim_type_t **eff) {
  const bim_subtypes_t *s = bim_subtypes_find(&r->st->schemas[r->f->schema], t);
  int ext = fuu_external(r->st, t, 0);
  int v;
  uint64_t c;
  *eff = t;
  if (!s && !ext) return 0;
  if (bim_rd_flag(&r->r, &v)) return -1;
  if (!v) return 0;
  if (ext) {
    if (bim_rd_flag(&r->r, &v)) return -1;
    if (v) return rd_global(r, 0, (const void **)eff);
  }
  if (!s) return range(r, "TypeIdentificationCode");
  if (bim_rd_bits(&r->r, bim_width((uint64_t)s->nsubs), &c)) return -1;
  if (c >= (uint64_t)s->nsubs || !s->subs[c]) return range(r, "TypeIdentificationCode");
  *eff = s->subs[c];
  return 0;
}

/* 6.6.5.3. *e replaced on substitution */
static int rd_subst(fr_t *r, const bim_part_t *p, const bim_elem_t **e) {
  const bim_elem_t *h = p ? p->elem : NULL;
  int ext = h && fuu_external(r->st, h, 1);
  int v;
  uint64_t c;
  if (!(h && h->nsubsts) && !ext) return 0;
  if (bim_rd_flag(&r->r, &v)) return -1;
  if (!v) return 0;
  if (ext) {
    if (bim_rd_flag(&r->r, &v)) return -1;
    if (v) return rd_global(r, 1, (const void **)e);
  }
  if (!h->nsubsts || bim_rd_bits(&r->r, bim_width((uint64_t)h->nsubsts), &c)) return h->nsubsts ? -1 : range(r, "SubstitutionSelect");
  if (c >= (uint64_t)h->nsubsts) return range(r, "SubstitutionSelect");
  *e = h->substs[c];
  return 0;
}

static bim_tbc_t *push(fr_t *r, bim_tbc_kind_t kind, const bim_type_t *parent, uint64_t max, int sel) {
  bim_fuu_t *f = r->f;
  bim_tbc_t *t;
  if (f->ntbcs >= TBC_MAX) {
    range(r, "context path length");
    return NULL;
  }
  if (f->ntbcs == r->cap) {
    size_t nc = r->cap ? r->cap * 2 : 8;
    bim_tbc_t *n = realloc(f->tbcs, nc * sizeof *n);
    if (!n) {
      bim_oom(r->r.err);
      return NULL;
    }
    f->tbcs = n;
    r->cap = nc;
  }
  r->ptype[f->ntbcs] = parent;
  r->pmax[f->ntbcs] = max;
  r->sel[f->ntbcs] = sel;
  t = &f->tbcs[f->ntbcs++];
  memset(t, 0, sizeof *t);
  t->kind = kind;
  t->type = parent;
  return t;
}

static int push_elem(fr_t *r, const bim_type_t *parent, uint64_t max, int sel, const char *name, const char *ns, const bim_type_t *t, const bim_type_t **cur) {
  const bim_type_t *eff;
  bim_tbc_t *tbc;
  if (rd_type_code(r, t, &eff)) return -1;
  tbc = push(r, BIM_TBC_ELEM, parent, max, sel);
  if (!tbc) return -1;
  tbc->name = name;
  tbc->ns = ns;
  tbc->type = eff;
  if (cur) *cur = eff;
  return 0;
}

/* 7.5.2.4.5.2 outside payload */
static int rd_any_elem(fr_t *r, const bim_elem_t **e) {
  uint64_t id;
  uint64_t len;
  uint64_t code;
  if (bim_rd_bits(&r->r, bim_width(bim_di_nall(r->st->di)), &id) || bim_rd_v5(&r->r, &len)) return -1;
  if (id >= r->st->nschemas || (int)id == r->st->opts.lax_schema) return range(r, "GlobalElementSchemaID");
  if (bim_rd_v5(&r->r, &code)) return -1;
  if (!r->st->schemas[id].known || code >= (uint64_t)r->st->schemas[id].nelems) return range(r, "Any_SBC_Operand_Selector");
  *e = r->st->schemas[id].elems[code];
  return 0;
}

/* 7.5.3.2, 7.5.3.3 outside payload: one attribute */
static int rd_any_attr(fr_t *r, bim_tbc_t *t) {
  int v;
  uint64_t id;
  uint64_t len;
  uint64_t n;
  if (bim_rd_flag(&r->r, &v)) return -1;
  if (!v) return range(r, "AnyAttributePresence");
  if (bim_rd_bits(&r->r, bim_width(bim_di_nall(r->st->di)), &id) || bim_rd_v5(&r->r, &len)) return -1;
  if (id >= r->st->nschemas) return range(r, "GlobalAttributeSchemaID");
  if ((int)id == r->st->opts.lax_schema) {
    unsigned char *q;
    if (bim_rd_v5(&r->r, &n) || bim_rd_v5(&r->r, &len)) return -1;
    if (n != 1) return range(r, "NumberOfLaxAnyAttributes");
    if (bim_rd_bytes(&r->r, len, &q)) return -1;
    t->heap_name = (char *)q;
    t->name = t->heap_name;
    t->owned = 1;
    t->simple = &bim_xs_string;
    if (bim_rd_v5(&r->r, &len) || bitreader_skip(&r->r.br, (size_t)len * 8)) return bim_rd_trunc(&r->r);
  } else {
    const bim_schema_t *sc = &r->st->schemas[id];
    if (bim_rd_v5(&r->r, &n)) return -1;
    if (!sc->known || n >= (uint64_t)sc->nattrs) return range(r, "GlobalAttributeID");
    t->name = sc->attrs[n].name;
    t->ns = sc->attrs[n].ns;
    t->simple = sc->attrs[n].simple;
    if (bim_rd_flag(&r->r, &v)) return -1;
    if (v) return range(r, "hasNextAttribute");
  }
  if (bim_rd_flag(&r->r, &v)) return -1;
  return v ? range(r, "hasNextSchema") : 0;
}

/* 6.6.5.2.3: selector, Path Termination Code in ContextTBC table */
static int rd_selector(fr_t *r, int operand, const bim_type_t **cur, int *end) {
  uint64_t sid = r->f->schema;
  uint64_t v;
  int w;
  if (r->st->di->add_schema) {
    if (bim_rd_bits(&r->r, bim_width(bim_di_nall(r->st->di)), &sid)) return -1;
    if (sid >= r->st->nschemas) return range(r, "SchemaIDOfSBC_Selector");
  }
  w = bim_width((uint64_t)r->st->schemas[sid].nelems + (operand ? 0 : 1));
  if (bim_rd_bits(&r->r, w, &v)) return -1;
  *end = !operand && v == (1ULL << w) - 1;
  if (*end) return push(r, BIM_TBC_END, NULL, 1, 1) ? 0 : -1;
  if (v >= (uint64_t)r->st->schemas[sid].nelems) return range(r, "SBC_Selector");
  {
    const bim_elem_t *e = r->st->schemas[sid].elems[v];
    return push_elem(r, NULL, 1, 1, e->name, e->ns, e->type, cur);
  }
}

/* 6.6.5.2.2 ContextTBC: parent 0, complex-typed children, all ones end */
static int rd_context_tbc(fr_t *r, const bim_type_t **stack, int *depth, int relative, int *end) {
  const bim_type_t *cur;
  if (*depth < 1) return range(r, "ContextTBC");
  cur = stack[*depth - 1];
  if (!cur) return range(r, "ContextTBC");
  int idx[TBC_MAX];
  int n = 0;
  int w;
  uint64_t v;
  const bim_sbc_t *s;
  const bim_part_t *p;
  const bim_elem_t *e = NULL;
  for (int i = 0; i < cur->nsbc && n < TBC_MAX; i++)
    if ((cur->sbc[i].kind == BIM_SBC_ELEM && fuu_is_complex(cur->sbc[i].type)) || cur->sbc[i].kind == BIM_SBC_ANY) idx[n++] = i;
  w = bim_width((uint64_t)n + 2);
  if (bim_rd_bits(&r->r, w, &v)) return -1;
  *end = v == (1ULL << w) - 1;
  if (*end) return push(r, BIM_TBC_END, cur, 1, 0) ? 0 : -1;
  if (!v) {
    if (!relative || *depth < 2) return range(r, "Reference to Parent");
    (*depth)--;
    return push(r, BIM_TBC_PARENT, cur, 1, 0) ? 0 : -1;
  }
  if (v > (uint64_t)n) return range(r, "SBC_Context");
  s = &cur->sbc[idx[v - 1]];
  p = fuu_part_of(cur, idx[v - 1]);
  if (s->kind == BIM_SBC_ANY ? rd_any_elem(r, &e) : rd_subst(r, p, &e)) return -1;
  if (*depth >= TBC_MAX) return range(r, "context path depth");
  return push_elem(r, cur, fuu_occ_max(p), 0, e ? e->name : s->name, e ? e->ns : s->ns, e ? e->type : s->type, &stack[(*depth)++]);
}

/* 6.6.5.2.2 OperandTBC: user data 0, mixed character data 1, children, attributes */
static int rd_operand_tbc(fr_t *r, const bim_type_t *cur) {
  int nel = 0;
  int nat = 0;
  int hs = 0;
  uint64_t v;
  uint64_t k;
  const bim_sbc_t *s;
  bim_tbc_t *tbc;
  for (int i = 0; i < cur->nsbc; i++) {
    if (fuu_is_elem_sbc(&cur->sbc[i])) nel++;
    else if (cur->sbc[i].kind == BIM_SBC_SIMPLE) hs = 1;
    else nat++;
  }
  if (bim_rd_bits(&r->r, bim_width((uint64_t)nel + (uint64_t)nat + (uint64_t)hs + 1 + (uint64_t)!!cur->mixed), &v)) return -1;
  if (!v) {
    r->f->user_data = 1;
    return 0;
  }
  if (cur->mixed && v == 1) {
    if (!(tbc = push(r, BIM_TBC_CHARDATA, cur, UNB, 0))) return -1;
    tbc->simple = &bim_xs_string;
    return 0;
  }
  k = v - 1 - (uint64_t)!!cur->mixed;
  if (k >= (uint64_t)cur->nsbc) return range(r, "SBC_Operand");
  s = &cur->sbc[k];
  if (s->kind == BIM_SBC_ANYATTR) return (tbc = push(r, BIM_TBC_ATTR, cur, 1, 0)) ? rd_any_attr(r, tbc) : -1;
  if (s->kind == BIM_SBC_ATTR || s->kind == BIM_SBC_SIMPLE) {
    if (!(tbc = push(r, s->kind == BIM_SBC_ATTR ? BIM_TBC_ATTR : BIM_TBC_SIMPLE, cur, 1, 0))) return -1;
    tbc->name = s->name;
    tbc->ns = s->ns;
    tbc->simple = s->simple;
    return 0;
  }
  {
    const bim_part_t *p = fuu_part_of(cur, (int)k);
    const bim_elem_t *e = NULL;
    if (s->kind == BIM_SBC_ANY ? rd_any_elem(r, &e) : rd_subst(r, p, &e)) return -1;
    return push_elem(r, cur, fuu_occ_max(p), 0, e ? e->name : s->name, e ? e->ns : s->ns, e ? e->type : s->type, NULL);
  }
}

static int rd_path_tbcs(fr_t *r, unsigned mode) {
  const bim_type_t *stack[TBC_MAX] = {NULL};
  int depth = 0;
  int end = 0;
  int relative = mode == 2 || mode == 4;
  if (relative) {
    if (!r->st->nctx) return bim_fail(r->r.err, BIM_FUU_STATE, "bim: FUU %s", "relative addressing without context node");
    for (int i = 0; i < r->st->nctx; i++) stack[depth++] = r->st->ctx_type[i];
  } else {
    if (rd_selector(r, 0, &stack[0], &end)) return -1;
    if (end) return rd_selector(r, 1, &stack[0], &end);
    depth = 1;
  }
  do {
    if (rd_context_tbc(r, stack, &depth, relative, &end)) return -1;
  } while (!end);
  return rd_operand_tbc(r, stack[depth - 1]);
}

/* ContextModeCode 101: path from ContextPathTable, position codes from FUU */
static int rd_table_path(fr_t *r) {
  const bim_decoderinit_t *di = r->st->di;
  uint64_t code;
  if (!di->cpt) return range(r, "ContextModeCode");
  if (bim_rd_bits(&r->r, (int)di->cpt_code_len, &code)) return -1;
  r->f->path_code = code;
  for (size_t i = 0; i < di->ncpaths; i++)
    if (di->cpaths[i].code == code) {
      bitreader_t keep = r->r.br;
      uint64_t v;
      int rc;
      bitreader_init(&r->r.br, di->cpaths[i].bits, (size_t)((di->cpaths[i].nbits + 7) / 8));
      rc = bitreader_get(&r->r.br, 3, &v) || v != 1 ? range(r, "ContextPathTable path") : rd_path_tbcs(r, 1);
      r->r.br = keep;
      return rc;
    }
  return range(r, "ContextPathCode");
}

static int rd_payloads(fr_t *r) {
  bim_fuu_t *f = r->f;
  const bim_tbc_t *op = &f->tbcs[f->ntbcs - 1];
  bim_decode_opts_t o = r->st->opts;
  int elem = op->kind == BIM_TBC_ELEM;
  o.schemas = r->st->schemas;
  if (r->st->di->unit_size_code) return bim_fail(r->r.err, BIM_FUU_UNSUP, "bim: FUU %s unsupported", "UnitSize");
  o.nschemas = (int)r->st->nschemas;
  o.mpegb = r->st->di->mpegb;
  if (elem ? !(f->payloads = calloc(f->npayloads, sizeof *f->payloads)) : !(f->values = calloc(f->npayloads, sizeof *f->values))) return bim_oom(r->r.err);
  for (size_t i = 0; i < f->npayloads; i++) {
    if (elem ? bim_decode_payload(op->type, op->name, &o, &r->r.br, NULL, &f->payloads[i]) : bim_decode_simple(op->simple, &o, &r->r.br, NULL, &f->values[i]))
      return range(r, "payload");
    if (elem) f->payloads[i]->ns = op->ns;
  }
  return 0;
}

/* 6.4.2, 8.2.1: 00 new instances and mappings, 01 keep, 10 DecoderInit's */
static int rd_reparam(fr_t *r, int v) {
  bim_fuu_state_t *st = r->st;
  bim_decoderinit_t cfg;
  bim_aod_t *a;
  if (v == 1) return 0;
  if (v == 0) {
    if (bim_di_aod_config_parse(&r->r.br, st->di, &cfg, r->r.err)) return -1;
    if (bim_aod_build(st->di, &cfg, st->schemas, (int)st->nschemas, &a, r->r.err)) {
      bim_decoderinit_free(&cfg);
      return -1;
    }
    if (!(r->f->config = malloc(sizeof cfg))) {
      bim_aod_free(a);
      bim_decoderinit_free(&cfg);
      return bim_oom(r->r.err);
    }
    *r->f->config = cfg;
    if (st->aod_cur != st->aod_default) bim_aod_free(st->aod_cur);
    st->aod_cur = a;
  } else {
    if (st->aod_cur != st->aod_default) bim_aod_free(st->aod_cur);
    st->aod_cur = st->aod_default;
  }
  st->opts.aod = st->aod_cur;
  return 0;
}

/* 6.4.2 after FUU_Length */
static int rd_fuu(fr_t *r) {
  const bim_decoderinit_t *di = r->st->di;
  bim_fuu_t *f = r->f;
  uint64_t v;
  if (bim_rd_bits(&r->r, 4, &v)) return -1;
  f->command = (unsigned)v;
  if (f->command < BIM_CMD_ADD || f->command > BIM_CMD_RESET) return range(r, "FragmentUpdateCommand");
  if (di->mpegb && di->aod) {
    if (bim_rd_bits(&r->r, 2, &v)) return -1;
    if (v == 3) return range(r, "OptimisedDecodersReparameterization");
    f->reparam = (int)v;
    if (rd_reparam(r, (int)v)) return -1;
  }
  if (f->command == BIM_CMD_RESET) return 0;
  if (bim_rd_bits(&r->r, bim_width(bim_di_nall(di)), &f->schema)) return -1;
  if (f->schema >= r->st->nschemas) return range(r, "SchemaID");
  if (bim_rd_bits(&r->r, 3, &v)) return -1;
  f->mode = (unsigned)v;
  if (!f->mode || f->mode > (di->mpegb ? 5u : 4u)) return range(r, "ContextModeCode");
  /* 6.6.3: relative modes keep SchemaID */
  if ((f->mode == 2 || f->mode == 4) && f->schema != r->st->last_schema) return range(r, "SchemaID");
  if (f->mode == 5 ? rd_table_path(r) : rd_path_tbcs(r, f->mode)) return -1;
  if (f->user_data) return 0;
  if (bim_fuu_rd_positions(r)) return -1;
  return f->command == BIM_CMD_DELETE ? 0 : rd_payloads(r);
}

int bim_fuu_parse(bim_fuu_state_t *st, const bim_bits_t *fuu, bim_fuu_t *f, bim_err_t *err) {
  fr_t *r = calloc(1, sizeof *r);
  int rc;
  memset(f, 0, sizeof *f);
  memset(err, 0, sizeof *err);
  if (!r) return bim_oom(err);
  r->r.err = err;
  r->r.trunc = BIM_FUU_TRUNC;
  r->r.what = "FUU";
  r->st = st;
  r->f = f;
  bitreader_init(&r->r.br, fuu->bits, (size_t)((fuu->nbits + 7) / 8));
  rc = rd_fuu(r);
  free(r);
  if (rc) bim_fuu_free(f);
  return rc;
}

void bim_fuu_free(bim_fuu_t *f) {
  for (size_t i = 0; i < f->ntbcs; i++)
    free(f->tbcs[i].heap_name);
  for (size_t i = 0; f->payloads && i < f->npayloads; i++) bim_node_free(f->payloads[i]);
  for (size_t i = 0; f->values && i < f->npayloads; i++) free(f->values[i]);
  free(f->payloads);
  free(f->values);
  free(f->tbcs);
  free(f->pos);
  if (f->config) bim_decoderinit_free(f->config);
  free(f->config);
  memset(f, 0, sizeof *f);
}
