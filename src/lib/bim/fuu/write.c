/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/fuu/priv.h"

typedef struct {
  bim_wr_t w;
  bim_fuu_state_t *st;
  const bim_fuu_t *f;
  const bim_type_t *ptype[TBC_MAX];
  uint64_t pmax[TBC_MAX];
  int sel[TBC_MAX];
} fw_t;

static int bad(fw_t *x, const char *what) { return bim_fail(x->w.err, BIM_FUU_RANGE, "bim: FUU %s out of range", what); }

static int same(const char *a, const char *b) { return (!a || !*a) && (!b || !*b) ? 1 : a && b && !strcmp(a, b); }

static int put(fw_t *x, uint64_t v, int n) { return bim_wr_put(&x->w, v, n); }

static int nall_bits(const fw_t *x) { return bim_width(bim_di_nall(x->st->di)); }

/* vluimsbf5 bit count, bits of tmp */
static int wr_counted(fw_t *x, bitwriter_t *tmp) {
  bim_bits_t b = {NULL, 0, NULL};
  size_t n;
  b.nbits = (uint64_t)tmp->len * 8 + (uint64_t)tmp->cur_bits;
  b.bits = bitwriter_data(tmp, &n);
  if (!b.bits && b.nbits) return bim_oom(x->w.err);
  return bim_wr_v5(&x->w, b.nbits) || (b.nbits && bim_wr_bits(&x->w, &b));
}

/* SchemaSwitching branch: SchemaID + global code */
static int wr_global(fw_t *x, int elems, const void *p) {
  size_t sid;
  uint64_t code;
  if (fuu_find_code(x->st, p, elems, &sid, &code)) return bad(x, "global code");
  return put(x, sid, nall_bits(x)) || put(x, code, bim_width((uint64_t)(elems ? x->st->schemas[sid].nelems : x->st->schemas[sid].ntypes)));
}

/* global element of a known schema by name, lax excluded */
static const bim_elem_t *find_elem(const fw_t *x, const char *name, const char *ns, size_t *sid, uint64_t *code) {
  for (size_t i = 0; i < x->st->nschemas; i++) {
    const bim_schema_t *sc = &x->st->schemas[i];
    if (!sc->known || (int)i == x->st->opts.lax_schema) continue;
    for (int c = 0; c < sc->nelems; c++)
      if (name && !strcmp(sc->elems[c]->name, name) && same(sc->elems[c]->ns, ns)) {
        if (sid) *sid = i;
        if (code) *code = (uint64_t)c;
        return sc->elems[c];
      }
  }
  return NULL;
}

/* 6.6.5.4 */
static int wr_type_code(fw_t *x, const bim_type_t *t, const bim_type_t *eff) {
  const bim_subtypes_t *s = bim_subtypes_find(&x->st->schemas[x->f->schema], t);
  int ext = fuu_external(x->st, t, 0);
  int idx = -1;
  if (!s && !ext) return eff == t ? 0 : bad(x, "TypeIdentificationCode");
  if (eff == t) return put(x, 0, 1);
  for (int i = 0; s && i < s->nsubs; i++)
    if (s->subs[i] == eff) idx = i;
  if (put(x, 1, 1) || (ext && put(x, idx < 0, 1))) return -1;
  if (idx < 0) return ext ? wr_global(x, 0, eff) : bad(x, "TypeIdentificationCode");
  return put(x, (uint64_t)idx, bim_width((uint64_t)s->nsubs));
}

/* 6.6.5.3. sub: element substituted */
static int wr_subst(fw_t *x, const bim_part_t *p, const bim_tbc_t *t, const char *name, const char *ns, const bim_elem_t **sub) {
  const bim_elem_t *h = p ? p->elem : NULL;
  int ext = h && fuu_external(x->st, h, 1);
  int idx = -1;
  const bim_elem_t *g;
  *sub = NULL;
  if (!(h && h->nsubsts) && !ext) return !strcmp(t->name, name) && same(t->ns, ns) ? 0 : bad(x, "SubstitutionSelect");
  if (!strcmp(t->name, name) && same(t->ns, ns)) return put(x, 0, 1);
  for (int i = 0; h && i < h->nsubsts; i++)
    if (!strcmp(h->substs[i]->name, t->name) && same(h->substs[i]->ns, t->ns)) idx = i;
  if (put(x, 1, 1)) return -1;
  if (idx >= 0) {
    *sub = h->substs[idx];
    return (ext && put(x, 0, 1)) || put(x, (uint64_t)idx, bim_width((uint64_t)h->nsubsts));
  }
  if (!ext) return bad(x, "SubstitutionSelect");
  g = find_elem(x, t->name, t->ns, NULL, NULL);
  if (!g) return bad(x, "SubstitutionSelect");
  *sub = g;
  return put(x, 1, 1) || wr_global(x, 1, g);
}

/* 7.5.2.4.5.2 outside payload */
static int wr_any_elem(fw_t *x, const bim_tbc_t *t, const bim_elem_t **e) {
  size_t sid;
  uint64_t code;
  bitwriter_t tmp;
  bim_wr_t tw = {&tmp, x->w.err};
  int rc;
  if (!(*e = find_elem(x, t->name, t->ns, &sid, &code))) return bad(x, "Any_SBC_Operand_Selector");
  bitwriter_init(&tmp);
  rc = bim_wr_v5(&tw, code) || put(x, sid, nall_bits(x)) || wr_counted(x, &tmp);
  bitwriter_free(&tmp);
  return rc;
}

/* 7.5.3.2, 7.5.3.3 outside payload: one attribute */
static int wr_any_attr(fw_t *x, const bim_tbc_t *t) {
  bitwriter_t tmp;
  bim_wr_t tw = {&tmp, x->w.err};
  int rc;
  uint64_t sid;
  bitwriter_init(&tmp);
  if (t->owned) {
    size_t n = strlen(t->name);
    sid = (uint64_t)x->st->opts.lax_schema;
    rc = bim_wr_v5(&tw, 1) || bim_wr_v5(&tw, n) || bim_wr_bytes(&tw, (const unsigned char *)t->name, n) || bim_wr_v5(&tw, 0);
  } else {
    size_t id = 0;
    int c = -1;
    for (size_t i = 0; i < x->st->nschemas && c < 0; i++) {
      const bim_schema_t *sc = &x->st->schemas[i];
      for (int k = 0; sc->known && (int)i != x->st->opts.lax_schema && k < sc->nattrs; k++)
        if (!strcmp(sc->attrs[k].name, t->name) && same(sc->attrs[k].ns, t->ns)) {
          id = i;
          c = k;
          break;
        }
    }
    if (c < 0) {
      bitwriter_free(&tmp);
      return bad(x, "Any_SBC_Operand_Selector");
    }
    sid = id;
    rc = bim_wr_v5(&tw, (uint64_t)c) || bim_wr_put(&tw, 0, 1);
  }
  rc = rc || put(x, 1, 1) || put(x, sid, nall_bits(x)) || wr_counted(x, &tmp) || put(x, 0, 1);
  bitwriter_free(&tmp);
  return rc;
}

static void book(fw_t *x, size_t i, const bim_type_t *pt, uint64_t max, int sel) {
  x->ptype[i] = pt;
  x->pmax[i] = max;
  x->sel[i] = sel;
}

/* 6.6.5.2.3. operand: no termination code */
static int wr_selector(fw_t *x, size_t i, int operand, const bim_type_t **cur) {
  const bim_tbc_t *t = &x->f->tbcs[i];
  const bim_decoderinit_t *di = x->st->di;
  size_t sid = x->f->schema;
  uint64_t k = 0;
  const bim_elem_t *e = NULL;
  int w;
  if (t->kind != BIM_TBC_END) {
    const bim_schema_t *sc = &x->st->schemas[sid];
    for (int c = 0; c < sc->nelems && !e; c++)
      if (!strcmp(sc->elems[c]->name, t->name) && same(sc->elems[c]->ns, t->ns)) {
        e = sc->elems[c];
        k = (uint64_t)c;
      }
    if (!e) e = find_elem(x, t->name, t->ns, &sid, &k);
    if (!e) return bad(x, "SBC_Selector");
    if (!di->add_schema && sid != x->f->schema) return bad(x, "SchemaIDOfSBC_Selector");
  }
  if (di->add_schema && put(x, sid, nall_bits(x))) return -1;
  w = bim_width((uint64_t)x->st->schemas[sid].nelems + (uint64_t)!operand);
  book(x, i, NULL, 1, 1);
  if (t->kind == BIM_TBC_END) return operand ? bad(x, "SBC_Selector") : put(x, (1ULL << w) - 1, w);
  if (put(x, k, w)) return -1;
  if (wr_type_code(x, e->type, t->type)) return -1;
  if (cur) *cur = t->type;
  return 0;
}

/* t: SBC entry's element, substitution group member, or global element for ANY */
static int sbc_match(const fw_t *x, const bim_type_t *cur, int k, const bim_tbc_t *t) {
  const bim_sbc_t *s = &cur->sbc[k];
  const bim_part_t *p = fuu_part_of(cur, k);
  const bim_elem_t *h = p ? p->elem : NULL;
  if (s->kind == BIM_SBC_ANY) return find_elem(x, t->name, t->ns, NULL, NULL) != NULL;
  if (s->kind != BIM_SBC_ELEM) return 0;
  if (!strcmp(s->name, t->name) && same(s->ns, t->ns)) return 1;
  for (int i = 0; h && i < h->nsubsts; i++)
    if (!strcmp(h->substs[i]->name, t->name) && same(h->substs[i]->ns, t->ns)) return 1;
  return h && fuu_external(x->st, h, 1) && find_elem(x, t->name, t->ns, NULL, NULL);
}

/* element reference of SBC entry k: SBC_Selector subst or any, then type code */
static int wr_elem_ref(fw_t *x, size_t i, const bim_type_t *cur, int k) {
  const bim_tbc_t *t = &x->f->tbcs[i];
  const bim_sbc_t *s = &cur->sbc[k];
  const bim_part_t *p = fuu_part_of(cur, k);
  const bim_elem_t *e = NULL;
  if (s->kind == BIM_SBC_ANY ? wr_any_elem(x, t, &e) : wr_subst(x, p, t, s->name, s->ns, &e)) return -1;
  book(x, i, cur, fuu_occ_max(p), 0);
  return wr_type_code(x, e ? e->type : s->type, t->type);
}

/* 6.6.5.2.2 ContextTBC */
static int wr_context_tbc(fw_t *x, size_t i, const bim_type_t **stack, int *depth, int relative) {
  const bim_tbc_t *t = &x->f->tbcs[i];
  const bim_type_t *cur;
  if (*depth < 1) return bad(x, "ContextTBC");
  cur = stack[*depth - 1];
  if (!cur) return bad(x, "ContextTBC");
  int idx[TBC_MAX];
  int n = 0;
  int w;
  for (int j = 0; j < cur->nsbc && n < TBC_MAX; j++)
    if ((cur->sbc[j].kind == BIM_SBC_ELEM && fuu_is_complex(cur->sbc[j].type)) || cur->sbc[j].kind == BIM_SBC_ANY) idx[n++] = j;
  w = bim_width((uint64_t)n + 2);
  book(x, i, cur, 1, 0);
  if (t->kind == BIM_TBC_END) return put(x, (1ULL << w) - 1, w);
  if (t->kind == BIM_TBC_PARENT) {
    if (!relative || *depth < 2) return bad(x, "Reference to Parent");
    (*depth)--;
    return put(x, 0, w);
  }
  if (t->kind != BIM_TBC_ELEM || *depth >= TBC_MAX) return bad(x, "SBC_Context");
  for (int c = 0; c < n; c++) {
    if (!sbc_match(x, cur, idx[c], t)) continue;
    if (put(x, (uint64_t)c + 1, w) || wr_elem_ref(x, i, cur, idx[c])) return -1;
    stack[(*depth)++] = t->type;
    return 0;
  }
  return bad(x, "SBC_Context");
}

/* 6.6.5.2.2 OperandTBC */
static int wr_operand(fw_t *x, size_t i, const bim_type_t *cur) {
  int nel = 0;
  int nat = 0;
  int hs = 0;
  int w;
  int k = -1;
  const bim_tbc_t *t = i < x->f->ntbcs ? &x->f->tbcs[i] : NULL;
  if (!cur) return bad(x, "SBC_Operand");
  for (int j = 0; j < cur->nsbc; j++) {
    if (fuu_is_elem_sbc(&cur->sbc[j])) nel++;
    else if (cur->sbc[j].kind == BIM_SBC_SIMPLE) hs = 1;
    else nat++;
  }
  w = bim_width((uint64_t)nel + (uint64_t)nat + (uint64_t)hs + 1 + (uint64_t)!!cur->mixed);
  if (x->f->user_data) return put(x, 0, w);
  if (!t) return bad(x, "SBC_Operand");
  if (t->kind == BIM_TBC_CHARDATA) {
    book(x, i, cur, UNB, 0);
    return cur->mixed ? put(x, 1, w) : bad(x, "SBC_Operand");
  }
  for (int j = 0; j < cur->nsbc && k < 0; j++) {
    const bim_sbc_t *s = &cur->sbc[j];
    int hit;
    if (t->kind == BIM_TBC_ELEM) hit = sbc_match(x, cur, j, t);
    else if (t->kind == BIM_TBC_SIMPLE) hit = s->kind == BIM_SBC_SIMPLE;
    else hit = s->kind == BIM_SBC_ATTR && !t->owned && !strcmp(s->name, t->name) && same(s->ns, t->ns);
    if (hit) k = j;
  }
  for (int j = 0; j < cur->nsbc && k < 0 && t->kind == BIM_TBC_ATTR; j++)
    if (cur->sbc[j].kind == BIM_SBC_ANYATTR) k = j;
  if (k < 0 || put(x, (uint64_t)k + 1 + (uint64_t)!!cur->mixed, w)) return k < 0 ? bad(x, "SBC_Operand") : -1;
  if (t->kind == BIM_TBC_ELEM) return wr_elem_ref(x, i, cur, k);
  book(x, i, cur, 1, 0);
  return cur->sbc[k].kind == BIM_SBC_ANYATTR ? wr_any_attr(x, t) : 0;
}

static int wr_path(fw_t *x, unsigned mode) {
  const bim_type_t *stack[TBC_MAX] = {NULL};
  const bim_fuu_t *f = x->f;
  int depth = 0;
  int relative = mode == 2 || mode == 4;
  size_t i = 0;
  if (!f->ntbcs) return bad(x, "context path length");
  if (relative) {
    if (!x->st->nctx) return bim_fail(x->w.err, BIM_FUU_STATE, "bim: FUU %s", "relative addressing without context node");
    for (int k = 0; k < x->st->nctx; k++) stack[depth++] = x->st->ctx_type[k];
  } else {
    if (f->tbcs[0].kind == BIM_TBC_END) return f->ntbcs == 2 ? wr_selector(x, 0, 0, NULL) || wr_selector(x, 1, 1, NULL) : bad(x, "context path length");
    if (wr_selector(x, 0, 0, &stack[0])) return -1;
    depth = 1;
    i = 1;
  }
  for (;; i++) {
    if (i >= f->ntbcs) return bad(x, "context path length");
    if (wr_context_tbc(x, i, stack, &depth, relative)) return -1;
    if (f->tbcs[i].kind == BIM_TBC_END) break;
  }
  return wr_operand(x, i + 1, stack[depth - 1]);
}

static uint64_t rank(const fw_t *x, uint64_t v) { return x->st->di->insert ? fuu_rat_index(v) : v; }

static int wr_pc(fw_t *x, uint64_t v, bim_pc_t kind, int w) {
  switch (kind) {
    case BIM_PC_RATIONAL: return bim_wr_rational(&x->w, v);
    case BIM_PC_V5: return bim_wr_v5(&x->w, v);
    default: return put(x, v, w);
  }
}

typedef struct {
  fw_t *x;
  const int *layers;
  int nl;
} ord_t;

static int cmp_rows(const ord_t *o, size_t a, size_t b) {
  size_t n = o->x->f->ntbcs;
  for (int l = 0; l < o->nl; l++) {
    uint64_t ra = rank(o->x, o->x->f->pos[a * n + (size_t)o->layers[l]]);
    uint64_t rb = rank(o->x, o->x->f->pos[b * n + (size_t)o->layers[l]]);
    if (ra != rb) return ra < rb ? -1 : 1;
  }
  return 0;
}

/* 6.6.5.6 increment codes from row a to row b */
static int wr_increments(fw_t *x, const ord_t *o, size_t a, size_t b, int w) {
  size_t n = x->f->ntbcs;
  uint64_t steps[TBC_MAX];
  uint64_t total = 0;
  uint64_t done = 0;
  int first = -1;
  uint64_t base = x->st->di->insert ? fuu_rat_order(0) : 0;
  for (int l = 0; l < o->nl; l++) {
    uint64_t from = first < 0 ? x->f->pos[a * n + (size_t)o->layers[l]] : base;
    uint64_t to = x->f->pos[b * n + (size_t)o->layers[l]];
    uint64_t rf = rank(x, from);
    uint64_t rt = rank(x, to);
    steps[l] = 0;
    if (rf == UNB || rt == UNB || rt < rf) return bad(x, "IncrementalPositionCode");
    if (first < 0 && rt == rf) continue;
    if (first < 0) first = l;
    steps[l] = rt - rf;
    total += steps[l];
  }
  if (first < 0 || !total) return bad(x, "IncrementalPositionCode");
  for (int l = first; l < o->nl; l++)
    for (uint64_t s = 0; s < steps[l]; s++)
      if ((++done != total && put(x, 0, w)) || put(x, (uint64_t)l + 1, w)) return -1;
  return 0;
}

/* 6.6.5.5, 6.6.5.6: positions of first payload, increments for rest. ord: payload order */
static int wr_positions(fw_t *x, size_t *ord, size_t nord) {
  const bim_fuu_t *f = x->f;
  size_t n = f->ntbcs;
  int layers[TBC_MAX];
  int nl = 0;
  ord_t o = {x, layers, 0};
  int multi = f->mode == 3 || f->mode == 4;
  int w;
  if (nord < f->npayloads) return bad(x, "payload count");
  for (size_t k = 0; k < f->npayloads; k++) ord[k] = k;
  if (!multi && f->npayloads != 1) return bad(x, "payload count");
  for (size_t i = 0; i < n; i++) {
    int cw;
    int mpc;
    bim_pc_t k = (bim_pc_t)bim_fuu_pc_coding(x->st, &f->tbcs[i], x->ptype[i], x->pmax[i], x->sel[i], &cw, &mpc);
    if (k != BIM_PC_NONE) layers[nl++] = (int)i;
  }
  o.nl = nl;
  for (size_t a = 1; multi && a < f->npayloads; a++)
    for (size_t b = a; b > 0 && cmp_rows(&o, ord[b - 1], ord[b]) > 0; b--) {
      size_t t = ord[b];
      ord[b] = ord[b - 1];
      ord[b - 1] = t;
    }
  for (int l = 0; l < nl; l++) {
    int cw;
    int mpc;
    bim_pc_t k = (bim_pc_t)bim_fuu_pc_coding(x->st, &f->tbcs[layers[l]], x->ptype[layers[l]], x->pmax[layers[l]], x->sel[layers[l]], &cw, &mpc);
    if (wr_pc(x, f->pos[ord[0] * n + (size_t)layers[l]], k, cw)) return -1;
  }
  if (!multi) return 0;
  w = bim_width((uint64_t)nl + 2);
  for (size_t k = 1; k < f->npayloads; k++)
    if (wr_increments(x, &o, ord[k - 1], ord[k], w)) return -1;
  return put(x, (1ULL << w) - 1, w);
}

static int wr_payloads(fw_t *x, const bim_encode_opts_t *eo, strrepo_writer_t *sw, const size_t *ord, size_t nord) {
  const bim_fuu_t *f = x->f;
  const bim_tbc_t *op = &f->tbcs[f->ntbcs - 1];
  if (nord < f->npayloads) return bad(x, "payload count");
  for (size_t k = 0; k < f->npayloads; k++) {
    size_t i = ord[k];
    if (op->kind == BIM_TBC_ELEM ? !f->payloads || bim_encode_payload_opts(op->type, f->payloads[i], eo, x->w.bw, sw) : !f->values || bim_encode_simple(op->simple, f->values[i], eo, x->w.bw, sw))
      return bad(x, "payload");
  }
  return 0;
}

int bim_fuu_write(bim_fuu_state_t *st, const bim_fuu_t *f, const bim_encode_opts_t *eo, strrepo_writer_t *sw, bitwriter_t *bw, bim_err_t *err) {
  const bim_decoderinit_t *di = st->di;
  fw_t *x = calloc(1, sizeof *x);
  size_t nord = f->npayloads ? f->npayloads : 1;
  size_t *ord = calloc(nord, sizeof *ord);
  bim_encode_opts_t o;
  int rc = -1;
  memset(err, 0, sizeof *err);
  if (!x || !ord) {
    free(x);
    free(ord);
    return bim_oom(err);
  }
  memset(&o, 0, sizeof o);
  if (eo) o = *eo;
  else o.lax_schema = -1;
  o.schemas = st->schemas;
  o.nschemas = (int)st->nschemas;
  o.mpegb = di->mpegb;
  x->w.bw = bw;
  x->w.err = err;
  x->st = st;
  x->f = f;
  if (f->command < BIM_CMD_ADD || f->command > BIM_CMD_RESET) {
    bad(x, "FragmentUpdateCommand");
    goto out;
  }
  if (put(x, f->command, 4)) goto out;
  if (di->mpegb && di->aod) {
    if (f->reparam < 0 || f->reparam > 2 || (f->reparam == 0 && !f->config)) {
      bad(x, "OptimisedDecodersReparameterization");
      goto out;
    }
    if (put(x, (uint64_t)f->reparam, 2) || (f->reparam == 0 && bim_di_aod_config_write(bw, di, f->config, err))) goto out;
  }
  if (f->command == BIM_CMD_RESET) {
    rc = 0;
    goto out;
  }
  if (f->schema >= st->nschemas || f->mode < 1 || f->mode > (di->mpegb ? 5u : 4u)) {
    bad(x, f->mode < 1 || f->mode > 5 ? "ContextModeCode" : "SchemaID");
    goto out;
  }
  if (put(x, f->schema, nall_bits(x)) || put(x, f->mode, 3)) goto out;
  if (f->mode == 5) {
    bitwriter_t scratch;
    bitwriter_t *keep = x->w.bw;
    bitwriter_init(&scratch);
    x->w.bw = &scratch;
    rc = wr_path(x, 1);
    x->w.bw = keep;
    bitwriter_free(&scratch);
    if (!rc && !di->cpt) rc = bad(x, "ContextModeCode");
    if (rc || put(x, f->path_code, (int)di->cpt_code_len)) {
      rc = -1;
      goto out;
    }
  } else if (wr_path(x, f->mode)) {
    goto out;
  }
  if (f->user_data) {
    rc = 0;
    goto out;
  }
  if (wr_positions(x, ord, nord)) goto out;
  rc = f->command == BIM_CMD_DELETE ? 0 : wr_payloads(x, &o, sw, ord, nord);
out:
  free(x);
  free(ord);
  return rc;
}
