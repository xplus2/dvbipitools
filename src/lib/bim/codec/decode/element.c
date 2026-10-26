/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/decode/priv.h"

static void string_resync(dec_t *d) {
  if (d->sr && !d->mpegb) d->sr->resync = d->br; /* next string carries string_offset, TS 102 323 table 58 */
}

static size_t str_pos(const dec_t *d) { return d->sr ? d->sr->pos : d->zpos; }

const bim_simple_t bim_xs_string = {.codec = BIM_UTF8};

/* ISO 23001-1 7.4.5. *idx -1: type unchanged. deferred, nil: sizeIncrease codes before subtypes */
static int type_code(dec_t *d, const bim_subtypes_t *s, int flag, int nillable, int *idx, int *deferred, int *nil) {
  uint64_t v = 1;
  uint64_t inc = (uint64_t)d->deferred + (uint64_t)!!nillable;
  uint64_t n = (uint64_t)(s ? s->nsubs : 0) + inc;
  *idx = -1;
  *deferred = 0;
  *nil = 0;
  if (flag && bitreader_get(d->br, 1, &v)) return -1;
  if (!v || !n) return 0;
  if (bitreader_get(d->br, bim_width((uint64_t)(int)n), &v) || v >= n) return -1;
  if (d->deferred && v == 0) *deferred = 1;
  else if (nillable && v == (uint64_t)d->deferred) *nil = 1;
  else *idx = (int)(v - inc);
  return 0;
}

static int set_nil(bim_node_t *k) {
  char *v = strdup("true");
  return v ? bim_node_add_attr_ns(k, XSI_NS, "nil", v) : -1;
}

/* 6.6.5.3 in payload (7.4.2). *e: element substituted, NULL keep */
static int subst_code(dec_t *d, const bim_part_t *p, const bim_elem_t **sub) {
  const bim_elem_t *e = p ? p->elem : NULL;
  int ext = e && d->opts && d->opts->ext_subst && d->opts->ext_subst(d->opts->ctx, e);
  uint64_t v;
  *sub = NULL;
  if (!(e && e->nsubsts) && !ext) return 0;
  if (bitreader_get(d->br, 1, &v)) return -1;
  if (!v) return 0;
  if (ext) {
    if (bitreader_get(d->br, 1, &v)) return -1;
    if (v) {
      uint64_t id;
      if (bitreader_get(d->br, bim_width((uint64_t)d->nschemas), &id) || id >= (uint64_t)d->nschemas) return -1;
      if (bitreader_get(d->br, bim_width((uint64_t)d->schemas[id].nelems), &v) || v >= (uint64_t)d->schemas[id].nelems) return -1;
      *sub = d->schemas[id].elems[v];
      return 0;
    }
  }
  if (!e->nsubsts || bitreader_get(d->br, bim_width((uint64_t)e->nsubsts), &v) || v >= (uint64_t)e->nsubsts) return -1;
  *sub = e->substs[v];
  return 0;
}

static void rename_node(bim_node_t *k, const bim_elem_t *e) {
  k->name = e->name;
  k->ns = e->ns;
}

static int decode_body(dec_t *d, const bim_type_t *t, bim_node_t *node);

static int decode_part(dec_t *d, const bim_part_t *p, bim_node_t *node, int mixed);

/* ISO 23001-1 7.4.2, ISO 15938-1 8.4.2 (Cor 2). top: no substitution or type code */
static int decode_mono(dec_t *d, const bim_part_t *p, const bim_type_t *t, bim_node_t *k, int top, int *deferred) {
  uint64_t v = 0;
  uint64_t len = 0;
  size_t start;
  int has_len = d->len_mode == 2;
  int idx = -1;
  int nil = 0;
  *deferred = 0;
  if (d->len_mode == 1) {
    if (bitreader_get(d->br, 1, &v)) return -1;
    has_len = v != 0;
  }
  if (has_len && bitreader_get_vluimsbf5(d->br, &len)) return -1;
  start = bitreader_bits_left(d->br);
  if (!top) {
    const bim_elem_t *sub;
    const bim_subtypes_t *s;
    int nillable = p ? p->nillable : 0;
    if (subst_code(d, p, &sub)) return -1;
    if (sub) {
      rename_node(k, sub);
      t = sub->type;
      nillable = sub->nillable;
    }
    s = d->cast ? bim_subtypes_find(&d->schemas[d->schema], t) : NULL;
    if ((s || d->deferred || nillable) && type_code(d, s, 1, nillable, &idx, deferred, &nil)) return -1;
    if (idx >= 0) {
      t = s->subs[idx];
      k->cast = t;
    }
  }
  if (nil && set_nil(k)) return -1;
  if (has_len && d->opts && d->opts->aod) d->zlen = d->zpos = 0;
  if (!*deferred && !nil && (!t || decode_body(d, t, k))) return -1;
  if (has_len && start - bitreader_bits_left(d->br) != len) return -1;
  if (has_len && d->opts && d->opts->aod) d->zlen = d->zpos = 0;
  if (has_len) string_resync(d);
  return 0;
}

/* ISO 23001-1 7.4.3, 7.4.4. unknown schema: rest skipped */
static int decode_multi(dec_t *d, const bim_part_t *p, const bim_type_t *t, bim_node_t *k, int *deferred) {
  uint64_t len;
  uint64_t v;
  size_t start;
  int first = 1;
  int rc = -1;
  int schema = d->schema;
  int nillable = p ? p->nillable : 0;
  *deferred = 0;
  if (bitreader_get_vluimsbf5(d->br, &len)) return -1;
  start = bitreader_bits_left(d->br);
  if (len > start || bitreader_get(d->br, 1, &v)) return -1;
  if (v) {
    uint64_t id;
    uint64_t code;
    if (bitreader_get(d->br, bim_width((uint64_t)d->nschemas), &id) || id >= (uint64_t)d->nschemas || bitreader_get_vluimsbf5(d->br, &code)) return -1;
    /* SubstituteElementCode of unknown schema ignored */
    if (d->schemas[id].known && code < (uint64_t)d->schemas[id].nelems) {
      const bim_elem_t *e = d->schemas[id].elems[code];
      rename_node(k, e);
      t = e->type;
      nillable = e->nillable;
    }
  }
  while (start - bitreader_bits_left(d->br) < len) {
    const bim_subtypes_t *s;
    const bim_type_t *body;
    int idx;
    int nil;
    if (bitreader_get(d->br, bim_width((uint64_t)d->nschemas), &v) || v >= (uint64_t)d->nschemas) goto done;
    if (!d->schemas[v].known || *deferred) {
      if (bitreader_skip(d->br, (size_t)len - (start - bitreader_bits_left(d->br)))) goto done;
      break;
    }
    d->schema = (int)v;
    s = bim_subtypes_find_x(&d->schemas[v], t);
    if (type_code(d, s, first, first && nillable, &idx, deferred, &nil)) goto done;
    if (nil) {
      if (set_nil(k)) goto done;
      first = 0;
      continue;
    }
    if (!*deferred) {
      body = idx < 0 ? (first ? t : NULL) : first ? s->subs[idx] : s->deltas ? s->deltas[idx] : NULL;
      if ((first || idx >= 0) && (!body || decode_body(d, body, k))) goto done;
      if (idx >= 0) {
        t = s->subs[idx];
        k->cast = t;
      }
    }
    first = 0;
  }
  if (start - bitreader_bits_left(d->br) == len) rc = 0;
done:
  d->schema = schema;
  if (!rc) string_resync(d);
  return rc;
}

/* ISO 23001-1 7.4.1, tables 11 and 12 */
int dec_element(dec_t *d, const bim_part_t *p, const bim_type_t *t, bim_node_t *k, int top, int *deferred) {
  int hot = d->hot;
  int multi = 0;
  int rc;
  if (!d->nofragref) return dec_fragment_reference(d, t, k, deferred);
  if (d->nschemas > 1 && hot && !(d->compat & BIM_COMPAT_NO_SCHEMA_MODE)) {
    uint64_t b1;
    uint64_t b2 = 0;
    uint64_t b3 = 0;
    if (bitreader_get(d->br, 1, &b1) || (b1 && bitreader_get(d->br, 1, &b2)) || (b2 && bitreader_get(d->br, 1, &b3))) return -1;
    /* 0 mono, 10 multi, 110 mono frozen, 111 multi children frozen */
    multi = b1 && (!b2 || b3);
    if (b2) d->hot = 0;
  }
  rc = multi ? decode_multi(d, p, t, k, deferred) : decode_mono(d, p, t, k, top, deferred);
  d->hot = hot;
  return rc;
}

/* 7.5.2.4.4: shunt 0 or mixed 1 plus string */
static int mixed_text(dec_t *d, bim_node_t *node) {
  uint64_t v;
  bim_node_t *k;
  char *text = NULL;
  if (bitreader_get(d->br, 1, &v)) return -1;
  if (!v) return 0;
  if (dec_value(d, d->schemas[d->schema].string ? d->schemas[d->schema].string : &bim_xs_string, &text)) return -1;
  if (!(k = bim_node_new(NULL)) || bim_node_add(node, k)) {
    free(text);
    bim_node_free(k);
    return -1;
  }
  k->text = text;
  return 0;
}

int dec_add_child(dec_t *d, bim_node_t *node, const bim_part_t *p, const bim_type_t *t, const char *name, const char *ns) {
  bim_node_t *k;
  int deferred;
  int rc;
  if (++d->nodes > MAX_NODES || d->level >= ELEM_DEPTH_MAX || !(k = bim_node_new(name))) return -1;
  k->ns = ns;
  d->level++;
  rc = dec_element(d, p, t, k, 0, &deferred);
  d->level--;
  if (rc || bim_node_add(node, k)) {
    bim_node_free(k);
    return -1;
  }
  if (deferred) k->deferred = 1;
  return 0;
}

/* 7.5.2.2.5.2.5: tree of choices over remaining children */
static int decode_all(dec_t *d, const bim_part_t *p, bim_node_t *node, int mixed) {
  int left[64];
  int n = p->nkids;
  if (n > 64) return -1;
  for (int i = 0; i < n; i++) left[i] = i;
  while (n) {
    uint64_t c = 0;
    int k;
    if (n > 1 && (bitreader_get(d->br, bim_width((uint64_t)n), &c) || c >= (uint64_t)n)) return -1;
    k = left[c];
    memmove(&left[c], &left[c + 1], (size_t)(n - 1 - (int)c) * sizeof *left);
    n--;
    if (decode_part(d, &p->kids[k], node, mixed)) return -1;
  }
  return 0;
}

static int decode_one(dec_t *d, const bim_part_t *p, bim_node_t *node, int mixed) {
  if (p->kind == BIM_PART_ELEM) {
    if (mixed && mixed_text(d, node)) return -1;
    return dec_add_child(d, node, p, p->type, p->name, p->ns);
  }
  if (p->kind == BIM_PART_ANY) {
    if (mixed && mixed_text(d, node)) return -1;
    return dec_any_element(d, node);
  }
  if (p->kind == BIM_PART_SEQ) {
    for (int i = 0; i < p->nkids; i++)
      if (decode_part(d, &p->kids[i], node, mixed)) return -1;
    return 0;
  }
  if (p->kind == BIM_PART_CHOICE) {
    uint64_t idx;
    if (bitreader_get(d->br, bim_width((uint64_t)p->nkids), &idx) || idx >= (uint64_t)p->nkids) return -1;
    return decode_part(d, &p->kids[idx], node, mixed); /* branch occurrences coded too */
  }
  if (p->kind == BIM_PART_ALL) return decode_all(d, p, node, mixed);
  return -1;
}

static int decode_part(dec_t *d, const bim_part_t *p, bim_node_t *node, int mixed) {
  uint64_t present = 1;
  uint64_t count = 0;
  int shunt;
  int w = occ_coding(p, &shunt);
  if (shunt && bitreader_get(d->br, 1, &present)) return -1;
  if (present && p->max == 1) {
    count = 1;
  } else if (present) {
    if (w < 0 ? bitreader_get_vluimsbf5(d->br, &count) : bitreader_get(d->br, w, &count)) return -1;
    if (count > UINT64_MAX - p->min) return -1;
    count += p->min;
    if (p->max != BIM_UNBOUNDED && count > p->max) return -1;
  }
  for (uint64_t i = 0; i < count; i++) {
    size_t bits = bitreader_bits_left(d->br);
    size_t spos = str_pos(d);
    if (decode_one(d, p, node, mixed)) return -1;
    if (count > 1 && bits == bitreader_bits_left(d->br) && spos == str_pos(d)) return -1;
  }
  return 0;
}

static int decode_body(dec_t *d, const bim_type_t *t, bim_node_t *node) {
  for (int i = 0; i < t->nattrs; i++) {
    const bim_attr_t *a = &t->attrs[i];
    uint64_t present = 1;
    char *v = NULL;
    if (!a->required && bitreader_get(d->br, 1, &present)) return -1;
    if (!present) continue;
    if (dec_value(d, a->simple, &v)) return -1;
    if (bim_node_add_attr_ns(node, a->ns, a->name, v)) return -1;
  }
  if (t->anyattr && dec_any_attributes(d, node)) return -1;
  if (t->content == BIM_CONTENT_SIMPLE) {
    char *v = NULL;
    if (dec_value(d, t->simple, &v)) return -1;
    free(node->text);
    node->text = v;
    return 0;
  }
  if (t->content == BIM_CONTENT_SEQ && decode_part(d, t->top, node, t->mixed)) return -1;
  /* 7.5.2.2.5.1: final shunt or mixed transition */
  return t->mixed && t->content != BIM_CONTENT_SIMPLE ? mixed_text(d, node) : 0;
}

int bim_decode_payload(const bim_type_t *t, const char *rootname, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr,
                       bim_node_t **out) {
  dec_t d;
  uint64_t modes;
  bim_node_t *root;
  int deferred;
  dec_init(&d, opts, br, sr);
  if (sr) {
    sr->base = sr->pos;
    sr->resync = NULL;
  }
  /* ISO 15938-1 8.3.2, ISO 23001-1 7.3.2 */
  if (bitreader_get(br, 8, &modes) || (modes >> 6) == 3) return -1;
  d.len_mode = (int)(modes >> 6);
  d.deferred = (int)(modes >> 5 & 1);
  d.cast = (int)(modes >> 4 & 1);
  d.nofragref = !d.mpegb || (int)(modes >> 3 & 1);
  root = bim_node_new(rootname);
  if (!root) return -1;
  if (dec_element(&d, NULL, t, root, 1, &deferred)) {
    bim_node_free(root);
    dec_done(&d);
    return -1;
  }
  if (deferred) root->deferred = 1;
  if (opts && opts->zleft) *opts->zleft = d.zlen - d.zpos;
  dec_done(&d);
  *out = root;
  return 0;
}
