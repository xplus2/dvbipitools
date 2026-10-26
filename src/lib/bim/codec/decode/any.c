/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/decode/priv.h"

/* moves content of a resolved fragment into k */
static void adopt(bim_node_t *k, bim_node_t *f) {
  free(k->text);
  k->text = f->text;
  f->text = NULL;
  for (int i = 0; i < k->nattrs; i++) free(k->attrs[i].value);
  free(k->attrs);
  k->attrs = f->attrs;
  k->nattrs = f->nattrs;
  f->attrs = NULL;
  f->nattrs = 0;
  for (int i = 0; i < f->nkids; i++) f->kids[i]->parent = k;
  for (int i = 0; i < k->nkids; i++) bim_node_free(k->kids[i]);
  free(k->kids);
  k->kids = f->kids;
  k->nkids = f->nkids;
  k->kcap = f->kcap;
  f->kids = NULL;
  f->nkids = 0;
  if (f->cast) k->cast = f->cast;
  bim_node_free(f);
}

/* ISO 23001-1 7.4.8, 7.4.9 */
int dec_fragment_reference(dec_t *d, const bim_type_t *t, bim_node_t *k, int *deferred) {
  uint64_t is_def;
  uint64_t specific;
  uint64_t v;
  uint64_t bits;
  unsigned format;
  char *href = NULL;
  const bim_decode_opts_t *o = d->opts;
  if (bitreader_get(d->br, 1, &is_def) || bitreader_get(d->br, 1, &specific) || bitreader_get(d->br, 6, &v)) return -1;
  if (specific) {
    if (bitreader_get(d->br, 8, &v)) return -1;
    format = (unsigned)v;
  } else {
    if (!o || !o->nfrag_formats) return -1;
    format = o->frag_formats[0];
  }
  if (bitreader_get_vluimsbf8(d->br, &bits) || bits > bitreader_bits_left(d->br)) return -1;
  if (format == 1) {
    size_t n = (size_t)(bits / 8);
    if (bits % 8 || !(href = malloc(n + 1))) return -1;
    if (bitreader_get_bytes(d->br, (unsigned char *)href, n) || memchr(href, '\0', n)) {
      free(href);
      return -1;
    }
    href[n] = '\0';
  } else if (bitreader_skip(d->br, (size_t)bits)) {
    return -1;
  }
  free(k->href);
  k->href = href;
  if (is_def) {
    k->deferred = 1;
    *deferred = 1;
    return 0;
  }
  *deferred = 0;
  {
    unsigned char *buf = NULL;
    size_t blen = 0;
    bitreader_t br;
    bim_node_t *f = NULL;
    int rc;
    /* 5.7.1: resolver returns a fragment update payload */
    if (!o || !o->resolve || !href || d->depth > 16 || o->resolve(o->ctx, href, format, &buf, &blen)) return -1;
    bitreader_init(&br, buf, blen);
    d->depth++;
    rc = bim_decode_payload(t, k->name, o, &br, d->sr, &f);
    d->depth--;
    free(buf);
    if (rc) return -1;
    adopt(k, f);
  }
  return 0;
}

/* 7.5.2.4.5.2 (Amd 2) */
int dec_any_element(dec_t *d, bim_node_t *node) {
  uint64_t id;
  uint64_t len;
  uint64_t code;
  size_t start;
  const bim_schema_t *sc;
  if (bitreader_get(d->br, bim_width((uint64_t)d->nschemas), &id) || id >= (uint64_t)d->nschemas || bitreader_get_vluimsbf5(d->br, &len)) return -1;
  start = bitreader_bits_left(d->br);
  if (len > start) return -1;
  sc = &d->schemas[id];
  if (d->opts && (int)id == d->opts->lax_schema) {
    bitreader_t sub = *d->br;
    if (dec_lax(d, &sub, (size_t)len, node)) return -1;
  } else if (sc->known && sc->nelems) {
    const bim_elem_t *e;
    int schema = d->schema;
    int rc;
    if (bitreader_get_vluimsbf5(d->br, &code) || code >= (uint64_t)sc->nelems) return -1;
    e = sc->elems[code];
    d->schema = (int)id;
    rc = dec_add_child(d, node, NULL, e->type, e->name, e->ns);
    d->schema = schema;
    if (rc) return -1;
    if (start - bitreader_bits_left(d->br) != len) return -1;
    return 0;
  }
  /* unknown schema or lax: AnyElement_Length skips */
  return bitreader_skip(d->br, (size_t)(len - (start - bitreader_bits_left(d->br))));
}

/* 7.5.3.2, 7.5.3.3 (Amd 2) */
int dec_any_attributes(dec_t *d, bim_node_t *node) {
  uint64_t v;
  if (bitreader_get(d->br, 1, &v)) return -1;
  if (!v) return 0;
  do {
    uint64_t id;
    uint64_t len;
    size_t start;
    const bim_schema_t *sc;
    if (bitreader_get(d->br, bim_width((uint64_t)d->nschemas), &id) || id >= (uint64_t)d->nschemas || bitreader_get_vluimsbf5(d->br, &len)) return -1;
    start = bitreader_bits_left(d->br);
    if (len > start) return -1;
    sc = &d->schemas[id];
    if (d->opts && (int)id == d->opts->lax_schema) {
      uint64_t n;
      if (bitreader_get_vluimsbf5(d->br, &n)) return -1;
      for (uint64_t i = 0; i < n; i++) {
        char *q = NULL;
        char *val = NULL;
        const char *qn;
        uint64_t ql;
        uint64_t vl;
        if (bitreader_get_vluimsbf5(d->br, &ql) || ql > bitreader_bits_left(d->br) / 8 || !(q = malloc((size_t)ql + 1)) || bitreader_get_bytes(d->br, (unsigned char *)q, (size_t)ql)) {
          free(q);
          return -1;
        }
        q[ql] = '\0';
        if (bitreader_get_vluimsbf5(d->br, &vl) || vl > bitreader_bits_left(d->br) / 8 || !(val = malloc((size_t)vl + 1)) || bitreader_get_bytes(d->br, (unsigned char *)val, (size_t)vl)) {
          free(q);
          free(val);
          return -1;
        }
        val[vl] = '\0';
        qn = bim_node_keep(node, q);
        free(q);
        if (!qn) {
          free(val);
          return -1;
        }
        if (bim_node_add_attr_ns(node, NULL, qn, val)) return -1;
      }
    } else if (sc->known && sc->nattrs) {
      do {
        uint64_t code;
        char *val = NULL;
        if (bitreader_get_vluimsbf5(d->br, &code) || code >= (uint64_t)sc->nattrs || dec_value(d, sc->attrs[code].simple, &val)) return -1;
        if (bim_node_add_attr_ns(node, sc->attrs[code].ns, sc->attrs[code].name, val)) return -1;
        if (bitreader_get(d->br, 1, &v)) return -1;
      } while (v);
    } else if (bitreader_skip(d->br, (size_t)len)) {
      return -1;
    }
    if (start - bitreader_bits_left(d->br) != len) return -1;
    if (bitreader_get(d->br, 1, &v)) return -1;
  } while (v);
  return 0;
}
