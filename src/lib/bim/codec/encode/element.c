/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/encode/priv.h"

int enc_counted(enc_t *e, int (*fn)(enc_t *e, void *arg), void *arg) {
  bitwriter_t tmp;
  bitwriter_t *keep = e->bw;
  int rc;
  bitwriter_init(&tmp);
  e->bw = &tmp;
  rc = fn(e, arg);
  e->bw = keep;
  if (!rc) rc = bitwriter_put_vluimsbf5(keep, (uint64_t)tmp.len * 8 + (uint64_t)tmp.cur_bits) || bitwriter_append(keep, &tmp);
  bitwriter_free(&tmp);
  return rc;
}

int enc_is_nil(const bim_node_t *k) {
  const char *v = bim_node_attr_ns(k, XSI_NS, "nil");
  return v && (!strcmp(v, "true") || !strcmp(v, "1"));
}

static int same_ns(const char *a, const char *b) { return (!a || !*a) && (!b || !*b) ? 1 : a && b && !strcmp(a, b); }

int enc_wildcard_allows(const bim_wildcard_t *w, const char *ns) {
  if (!w || w->kind == BIM_WC_ANY) return 1;
  if (w->kind == BIM_WC_NOT) return !same_ns(w->ns[0], ns);
  for (int i = 0; i < w->nns; i++)
    if (same_ns(w->ns[i], ns)) return 1;
  return 0;
}

int enc_extra_attr(const bim_type_t *t, const bim_node_t *node, int i) {
  const char *ns = node->attrs[i].ns;
  if (ns && !strcmp(ns, XSI_NS)) return 0;
  for (int j = 0; j < t->nattrs; j++)
    if (same_ns(t->attrs[j].ns, ns) && !strcmp(t->attrs[j].name, node->attrs[i].name)) return 0;
  return 1;
}

/* attribute i of node coded by t or declared fixed */
static int attr_known(const bim_type_t *t, const bim_node_t *node, int i) {
  if (!enc_extra_attr(t, node, i)) return 1;
  for (int j = 0; j < t->nfixed; j++)
    if (same_ns(t->fixed[j].ns, node->attrs[i].ns) && !strcmp(t->fixed[j].name, node->attrs[i].name)) return 1;
  return 0;
}

/* attributes of k declared by effective type t, wildcard takes any. diag names first undeclared */
static int attrs_declared(const enc_t *e, const bim_type_t *t, const bim_node_t *k) {
  if (e->dry || !t || t->anyattr) return 0;
  for (int i = 0; i < k->nattrs; i++) {
    if (attr_known(t, k, i)) continue;
    if (e->o->diag && e->o->ndiag)
      snprintf(e->o->diag, e->o->ndiag, "attribute %s%s%s of %s not declared", k->attrs[i].ns ? k->attrs[i].ns : "", k->attrs[i].ns ? ":" : "", k->attrs[i].name, k->name);
    return -1;
  }
  return 0;
}

typedef struct {
  const bim_part_t *p;
  const bim_type_t *t;
  const bim_node_t *k;
  int top;
} el_t;

typedef struct {
  const bim_elem_t *e; /* NULL: not substituted */
  int ext; /* element of another schema, 6.7.5.4 */
  int idx; /* SubstitutionSelect */
} subst_t;

static const bim_simple_t *string_simple(const enc_t *e) { return e->schemas[e->schema].string ? e->schemas[e->schema].string : &bim_xs_string; }

static int has_ext(const enc_t *e, const bim_elem_t *h) { return h && e->o->ext_subst && e->o->ext_subst(e->o->ctx, h); }

/* -1: k is no occurrence of p */
static int find_subst(const enc_t *e, const bim_part_t *p, const bim_node_t *k, subst_t *s) {
  const bim_elem_t *h = p ? p->elem : NULL;
  memset(s, 0, sizeof *s);
  if (!p || bim_node_is(k, p->ns, p->name)) return 0;
  for (int i = 0; h && i < h->nsubsts; i++)
    if (bim_node_is(k, h->substs[i]->ns, h->substs[i]->name)) {
      s->e = h->substs[i];
      s->idx = i;
      return 0;
    }
  if (has_ext(e, h) && !enc_global_name(e, k, &s->e, NULL, NULL)) {
    s->ext = 1;
    return 0;
  }
  return -1;
}

/* 6.6.5.3 in payload (7.4.2) */
static int put_subst(enc_t *e, const bim_part_t *p, const subst_t *s) {
  const bim_elem_t *h = p ? p->elem : NULL;
  int ext = has_ext(e, h);
  int sid;
  uint64_t code;
  if (!(h && h->nsubsts) && !ext) return 0;
  if (enc_bit(e->bw, s->e != NULL)) return -1;
  if (!s->e) return 0;
  if (ext) {
    if (enc_bit(e->bw, s->ext)) return -1;
    if (s->ext) {
      if (enc_global_ptr(e, s->e, &sid, &code)) return -1;
      return bitwriter_put(e->bw, (uint64_t)sid, bim_width((uint64_t)e->nschemas)) || bitwriter_put(e->bw, code, bim_width((uint64_t)e->schemas[sid].nelems));
    }
  }
  return bitwriter_put(e->bw, (uint64_t)s->idx, bim_width((uint64_t)h->nsubsts));
}

/* 7.4.5. idx -1: type unchanged. flag: leading presence bit, first chunk only in multi version */
static int put_type_code(enc_t *e, const bim_subtypes_t *s, int flag, int nillable, int idx, int deferred, int nil) {
  uint64_t inc = (uint64_t)e->o->deferred + (uint64_t)!!nillable;
  uint64_t n = (uint64_t)(s ? s->nsubs : 0) + inc;
  int coded = idx >= 0 || deferred || nil;
  uint64_t code = deferred ? 0 : nil ? (uint64_t)e->o->deferred : inc + (uint64_t)idx;
  if (flag && enc_bit(e->bw, coded)) return -1;
  if (flag && !coded) return 0;
  if (!n) return coded ? -1 : 0;
  return coded ? bitwriter_put(e->bw, code, bim_width(n)) : -1;
}

static int cast_index(const bim_subtypes_t *s, const bim_type_t *c) {
  for (int i = 0; s && i < s->nsubs; i++)
    if (s->subs[i] == c) return i;
  return -1;
}

static int next_elem(const bim_node_t *node, int ci, int mixed) {
  while (mixed && ci < node->nkids && !node->kids[ci]->name) ci++;
  return ci;
}

/* 7.5.2.4.4: kids [from, to) text before next element */
static int put_mixed(enc_t *e, const bim_node_t *node, int from, int to) {
  if (to - from > 1) return -1;
  if (to == from) return enc_bit(e->bw, 0);
  return enc_bit(e->bw, 1) || enc_text(e, string_simple(e), node->kids[from]->text ? node->kids[from]->text : "");
}

static int enc_body(enc_t *e, const bim_type_t *t, const bim_node_t *node, int *ci, int last);

static int walk_part(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci, int mixed);

static int encode_occurrences(const enc_t *e, const bim_part_t *p, int count) {
  int shunt;
  int w = occ_coding(p, &shunt);
  uint64_t n = (uint64_t)count - p->min;
  if ((uint64_t)count < p->min || (p->max != BIM_UNBOUNDED && (uint64_t)count > p->max)) return -1;
  if (shunt) {
    if (enc_bit(e->bw, count)) return -1;
    if (!count) return 0;
  }
  if (w < 0) return bitwriter_put_vluimsbf5(e->bw, n);
  return bitwriter_put(e->bw, n, w);
}

static int walk_all(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci, int mixed) {
  int left[64];
  int n = p->nkids;
  if (n > 64) return -1;
  for (int i = 0; i < n; i++) left[i] = i;
  while (n) {
    int pick = 0;
    int k;
    for (int c = 0; c < n; c++) {
      enc_t dry = *e;
      int cc = *ci;
      dry.dry = 1;
      if (!walk_part(&dry, &p->kids[left[c]], node, &cc, mixed) && cc > *ci) {
        pick = c;
        break;
      }
    }
    if (n > 1 && !e->dry && bitwriter_put(e->bw, (uint64_t)pick, bim_width((uint64_t)n))) return -1;
    k = left[pick];
    memmove(&left[pick], &left[pick + 1], (size_t)(n - 1 - pick) * sizeof *left);
    n--;
    if (walk_part(e, &p->kids[k], node, ci, mixed)) return -1;
  }
  return 0;
}

static int walk_one(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci, int mixed) {
  if (p->kind == BIM_PART_ELEM || p->kind == BIM_PART_ANY) {
    int j = next_elem(node, *ci, mixed);
    const bim_node_t *k;
    subst_t s;
    if (j >= node->nkids || !node->kids[j]->name) return -1;
    k = node->kids[j];
    if (p->kind == BIM_PART_ELEM ? !p->type || find_subst(e, p, k, &s) : !enc_wildcard_allows(p->wc, k->ns)) return -1;
    if (!e->dry) {
      if (mixed && put_mixed(e, node, *ci, j)) return -1;
      if (p->kind == BIM_PART_ELEM ? enc_element(e, p, p->type, k, 0) : enc_any_element(e, k)) return -1;
    }
    *ci = j + 1;
    return 0;
  }
  if (p->kind == BIM_PART_SEQ) {
    for (int i = 0; i < p->nkids; i++)
      if (walk_part(e, &p->kids[i], node, ci, mixed)) return -1;
    return 0;
  }
  if (p->kind == BIM_PART_CHOICE) {
    int pick = -1;
    int empty = -1;
    for (int i = 0; i < p->nkids && pick < 0; i++) {
      enc_t dry = *e;
      int c = *ci;
      dry.dry = 1;
      if (walk_part(&dry, &p->kids[i], node, &c, mixed)) continue;
      if (c > *ci) pick = i;
      else if (empty < 0) empty = i;
    }
    if (pick < 0) pick = empty;
    if (pick < 0) return -1;
    if (!e->dry && bitwriter_put(e->bw, (uint64_t)pick, bim_width((uint64_t)p->nkids))) return -1;
    return walk_part(e, &p->kids[pick], node, ci, mixed); /* branch occurrences coded too */
  }
  return p->kind == BIM_PART_ALL ? walk_all(e, p, node, ci, mixed) : -1;
}

static size_t out_mark(const enc_t *e) { return e->bw->len * 8 + (size_t)e->bw->cur_bits + (e->sw ? e->sw->len : 0) + enc_zprogress(e); }

static int walk_part(enc_t *e, const bim_part_t *p, const bim_node_t *node, int *ci, int mixed) {
  enc_t dry = *e;
  int cur = *ci;
  int count = 0;
  dry.dry = 1;
  while (p->max == BIM_UNBOUNDED || (uint64_t)count < p->max) {
    int c = cur;
    if (walk_one(&dry, p, node, &c, mixed)) break;
    if (c == cur) {
      if ((uint64_t)count < p->min) count = (int)p->min; /* empty occurrences */
      break;
    }
    cur = c;
    count++;
  }
  if ((uint64_t)count < p->min) return -1;
  if (e->dry) {
    *ci = cur;
    return 0;
  }
  if (encode_occurrences(e, p, count)) return -1;
  for (int i = 0; i < count; i++) {
    size_t mark = out_mark(e);
    if (walk_one(e, p, node, ci, mixed)) return -1;
    if (count > 1 && mark == out_mark(e)) return -1; /* decoder rejects */
  }
  return 0;
}

static int enc_body(enc_t *e, const bim_type_t *t, const bim_node_t *node, int *ci, int last) {
  for (int i = 0; i < t->nattrs; i++) {
    const bim_attr_t *a = &t->attrs[i];
    const char *v = bim_node_attr_ns(node, a->ns, a->name);
    if (!a->required && enc_bit(e->bw, v != NULL)) return -1;
    if (!v) {
      if (a->required) return -1;
      continue;
    }
    if (enc_text(e, a->simple, v)) return -1;
  }
  if (t->anyattr && enc_any_attributes(e, t, node)) return -1;
  if (t->content == BIM_CONTENT_SIMPLE) return enc_text(e, t->simple, node->text ? node->text : "");
  if (t->content == BIM_CONTENT_SEQ && walk_part(e, t->top, node, ci, t->mixed)) return -1;
  if (t->mixed) {
    int j = next_elem(node, *ci, 1);
    if (put_mixed(e, node, *ci, j)) return -1;
    *ci = j;
  }
  return last && *ci != node->nkids ? -1 : 0;
}

/* ISO 23001-1 7.4.2 body: substitution, type, body. top: neither code */
static int mono_inner(enc_t *e, void *arg) {
  el_t *a = arg;
  const bim_type_t *t = a->t;
  int ci = 0;
  if (!a->top) {
    subst_t s;
    int nillable = a->p ? a->p->nillable : 0;
    const bim_subtypes_t *st;
    int idx = -1;
    int deferred = a->k->deferred;
    int nil = enc_is_nil(a->k);
    if (find_subst(e, a->p, a->k, &s) || put_subst(e, a->p, &s)) return -1;
    if (s.e) {
      t = s.e->type;
      nillable = s.e->nillable;
    }
    st = e->o->cast ? bim_subtypes_find(&e->schemas[e->schema], t) : NULL;
    if (a->k->cast && a->k->cast != t && (idx = cast_index(st, a->k->cast)) < 0) return -1;
    if ((deferred && !e->o->deferred) || (nil && !nillable)) return -1;
    if ((st || e->o->deferred || nillable) && put_type_code(e, st, 1, nillable, idx, deferred, nil)) return -1;
    if (deferred || nil) return 0;
    if (idx >= 0) t = st->subs[idx];
  }
  return t && !attrs_declared(e, t, a->k) ? enc_body(e, t, a->k, &ci, 1) : -1;
}

static int enc_mono(enc_t *e, el_t *a) {
  int has_len = e->o->len_mode == 2;
  int rc;
  if (e->o->len_mode == 1) {
    has_len = a->t && (a->t->content == BIM_CONTENT_SEQ || a->t->nattrs);
    if (enc_bit(e->bw, has_len)) return -1;
  }
  rc = has_len ? enc_counted(e, mono_inner, a) : mono_inner(e, a);
  if (!rc && has_len && !e->o->mpegb) e->resync = 1;
  return rc;
}

static int home_schema(const enc_t *e, const bim_type_t *t) {
  for (int i = 0; i < e->nschemas; i++)
    for (int c = 0; e->schemas[i].known && c < e->schemas[i].ntypes; c++)
      if (e->schemas[i].types[c] == t) return i;
  return e->schema;
}

/* type after substitution */
static const bim_type_t *eff_type(const enc_t *e, const el_t *a) {
  subst_t s;
  return !find_subst(e, a->p, a->k, &s) && s.e ? s.e->type : a->t;
}

/* cast not in subtype table of base in home schema */
static int cast_needs_multi(const enc_t *e, const el_t *a) {
  const bim_type_t *t = eff_type(e, a);
  return !a->top && a->k->cast && a->k->cast != t && cast_index(bim_subtypes_find(&e->schemas[home_schema(e, t)], t), a->k->cast) < 0;
}

/* ISO 23001-1 7.4.3, 7.4.4: chunk of home schema, second for cast of another schema */
static int multi_inner(enc_t *e, void *arg) {
  el_t *a = arg;
  const bim_type_t *t = a->t;
  const bim_type_t *cast = a->k->cast != a->t ? a->k->cast : NULL;
  const bim_subtypes_t *s1;
  const bim_subtypes_t *s2 = NULL;
  subst_t s;
  int nillable = a->p ? a->p->nillable : 0;
  int deferred = a->k->deferred;
  int nil = enc_is_nil(a->k);
  int schema = e->schema;
  int sid;
  uint64_t code;
  int home;
  int idx = -1;
  int sid2 = -1;
  int idx2 = -1;
  int ci = 0;
  int rc = -1;
  if (find_subst(e, a->p, a->k, &s) || enc_bit(e->bw, s.e != NULL)) return -1;
  if (s.e) {
    if (enc_global_ptr(e, s.e, &sid, &code) || bitwriter_put(e->bw, (uint64_t)sid, bim_width((uint64_t)e->nschemas)) || bitwriter_put_vluimsbf5(e->bw, code)) return -1;
    t = s.e->type;
    nillable = s.e->nillable;
  }
  if ((deferred && !e->o->deferred) || (nil && !nillable)) return -1;
  home = home_schema(e, t);
  s1 = bim_subtypes_find(&e->schemas[home], t);
  if (cast && cast != t && (idx = cast_index(s1, cast)) < 0) {
    for (int v = 0; v < e->nschemas && sid2 < 0; v++) {
      if (v == home || !e->schemas[v].known) continue;
      s2 = bim_subtypes_find_x(&e->schemas[v], t);
      if ((idx2 = cast_index(s2, cast)) >= 0) sid2 = v;
    }
    if (sid2 < 0) return -1;
  }
  if (!deferred && !nil && attrs_declared(e, idx >= 0 ? s1->subs[idx] : sid2 >= 0 ? cast : t, a->k)) return -1;
  e->schema = home;
  if (bitwriter_put(e->bw, (uint64_t)home, bim_width((uint64_t)e->nschemas)) || put_type_code(e, s1, 1, nillable, idx, deferred, nil)) goto done;
  if (deferred || nil) {
    rc = 0;
    goto done;
  }
  if (sid2 < 0) {
    rc = enc_body(e, idx >= 0 ? s1->subs[idx] : t, a->k, &ci, 1);
  } else {
    const bim_type_t *delta = s2->deltas ? s2->deltas[idx2] : NULL;
    if (!delta || enc_body(e, t, a->k, &ci, 0)) goto done;
    e->schema = sid2;
    if (bitwriter_put(e->bw, (uint64_t)sid2, bim_width((uint64_t)e->nschemas)) || put_type_code(e, s2, 0, 0, idx2, 0, 0)) goto done;
    rc = enc_body(e, delta, a->k, &ci, 1);
  }
done:
  e->schema = schema;
  return rc;
}

int enc_element(enc_t *e, const bim_part_t *p, const bim_type_t *t, const bim_node_t *k, int top) {
  el_t a = {p, t, k, top};
  int hot = e->hot;
  int multi = 0;
  int rc;
  if (e->o->fragref) return enc_fragment_reference(e, k);
  if (e->nschemas > 1 && hot && !(e->o->compat & BIM_COMPAT_NO_SCHEMA_MODE)) {
    int freeze = top && e->o->freeze;
    multi = e->o->multi == 2 || (e->o->multi == 1 && cast_needs_multi(e, &a));
    /* 0 mono, 10 multi, 110 mono frozen, 111 multi frozen */
    if (freeze ? bitwriter_put(e->bw, multi ? 7 : 6, 3) : bitwriter_put(e->bw, multi ? 2 : 0, multi ? 2 : 1)) return -1;
    if (freeze) e->hot = 0;
  }
  if (multi) {
    rc = enc_counted(e, multi_inner, &a);
    if (!rc && !e->o->mpegb) e->resync = 1;
  } else {
    rc = enc_mono(e, &a);
  }
  e->hot = hot;
  return rc;
}

/* ISO 23001-1 7.4.8, 7.4.9 */
int enc_fragment_reference(enc_t *e, const bim_node_t *k) {
  size_t n = k->href && e->o->frag_format == 1 ? strlen(k->href) : 0;
  if (!k->href && e->o->frag_format == 1) return -1;
  if (bitwriter_put(e->bw, (uint64_t)!!k->deferred, 1) || bitwriter_put(e->bw, 1, 1) || bitwriter_put(e->bw, 0, 6) || bitwriter_put(e->bw, e->o->frag_format, 8)) return -1;
  if (bitwriter_put_vluimsbf8(e->bw, (uint64_t)n * 8)) return -1;
  return n ? bitwriter_put_bytes(e->bw, (const unsigned char *)k->href, n) : 0;
}

static int enc_init(enc_t *e, bim_encode_opts_t *def, const bim_encode_opts_t *o, bitwriter_t *bw, strrepo_writer_t *sw) {
  static const bim_schema_t one = {.known = 1};
  memset(def, 0, sizeof *def);
  memset(e, 0, sizeof *e);
  if (!o) {
    def->lax_schema = -1;
    def->frag_format = 1;
    o = def;
  }
  if (!sw || o->len_mode < 0 || o->len_mode > 2) return -1;
  e->o = o;
  e->bw = bw;
  e->sw = sw;
  e->schemas = o->nschemas ? o->schemas : &one;
  e->nschemas = o->nschemas ? o->nschemas : 1;
  e->hot = 1;
  e->st.cs = o->cs;
  e->zsize = o->zbuf_size ? o->zbuf_size : ZLIB_BUF_MAX;
  sw->base = sw->len;
  return 0;
}

int bim_encode_payload_opts(const bim_type_t *t, const bim_node_t *root, const bim_encode_opts_t *o, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_encode_opts_t def;
  enc_t e;
  int rc;
  if (enc_init(&e, &def, o, bw, sw)) return -1;
  o = e.o;
  if (o->diag && o->ndiag) o->diag[0] = '\0';
  /* ISO 15938-1 8.3.2, ISO 23001-1 7.3.2 */
  rc = bitwriter_put(bw, (uint64_t)o->len_mode << 6 | (uint64_t)!!o->deferred << 5 | (uint64_t)!!o->cast << 4 | (o->mpegb ? (uint64_t)!o->fragref << 3 | 7 : 0xF), 8) || (o->zgather ? enc_gathered(&e, t, root) : enc_element(&e, NULL, t, root, 1));
  sw->resync = NULL;
  return rc ? -1 : 0;
}

int bim_encode_simple(const bim_simple_t *s, const char *v, const bim_encode_opts_t *o, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_encode_opts_t def;
  enc_t e;
  int rc;
  if (enc_init(&e, &def, o, bw, sw)) return -1;
  rc = enc_text(&e, s, v);
  sw->resync = NULL;
  return rc ? -1 : 0;
}

int bim_encode_payload(const bim_type_t *t, const bim_node_t *root, int mpegb, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_encode_opts_t o;
  memset(&o, 0, sizeof o);
  o.mpegb = mpegb;
  o.lax_schema = -1;
  o.frag_format = 1;
  return bim_encode_payload_opts(t, root, &o, bw, sw);
}
