/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/encode/priv.h"
#include "lib/bim/codec/laxsym.h"

typedef struct {
  bitwriter_t bw;
  lax_syms_t sy;
  char **ns;
  int nns;
  struct {
    int ns;
    const char *name;
  } *el, *at;
  int nel;
  int nat;
} lw_t;

static int event(lw_t *l, uint32_t ev) {
  int code = lax_syms_code(&l->sy, ev);
  if (code >= 0 ? bitwriter_put(&l->bw, (uint64_t)code, 8) : ev > 32767 || bitwriter_put(&l->bw, 1u << 15 | ev, 16)) return -1;
  lax_syms_event(&l->sy, ev);
  return 0;
}

static int str(lw_t *l, const char *s) {
  size_t n = strlen(s);
  return (n && bitwriter_put_bytes(&l->bw, (const unsigned char *)s, n)) || bitwriter_put(&l->bw, 0, 8);
}

static int ns_id(lw_t *l, const char *ns, int *id) {
  char **n;
  ns = ns ? ns : "";
  for (int i = 0; i < l->nns; i++)
    if (!strcmp(l->ns[i], ns)) {
      *id = i;
      return 0;
    }
  if (l->nns >= 256 || !(n = realloc(l->ns, sizeof *n * (size_t)(l->nns + 1)))) return -1;
  l->ns = n;
  if (!(l->ns[l->nns] = strdup(ns))) return -1;
  *id = l->nns++;
  return event(l, 1) || str(l, ns);
}

/* ev 2 element name, 3 attribute name: nsid byte + name. *code: index */
static int name_id(lw_t *l, int ev, const char *ns, const char *name, int *code) {
  int id;
  int *n = ev == 2 ? &l->nel : &l->nat;
  void *grown;
  if (ns_id(l, ns, &id)) return -1;
  for (int i = 0; i < *n; i++) {
    const char *known = ev == 2 ? l->el[i].name : l->at[i].name;
    int kns = ev == 2 ? l->el[i].ns : l->at[i].ns;
    if (kns == id && !strcmp(known, name)) {
      *code = i;
      return 0;
    }
  }
  if (!(grown = realloc(ev == 2 ? (void *)l->el : (void *)l->at, sizeof *l->el * (size_t)(*n + 1)))) return -1;
  if (ev == 2) {
    l->el = grown;
    l->el[*n].ns = id;
    l->el[*n].name = name;
  } else {
    l->at = grown;
    l->at[*n].ns = id;
    l->at[*n].name = name;
  }
  *code = (*n)++;
  return event(l, (uint32_t)ev) || bitwriter_put(&l->bw, (uint64_t)id, 8) || str(l, name);
}

static int walk(lw_t *l, const bim_node_t *k) {
  int eid;
  int aid;
  if (!k->name) return event(l, 6) || str(l, k->text ? k->text : "");
  if (name_id(l, 2, k->ns, k->name, &eid)) return -1;
  for (int i = 0; i < k->nattrs; i++)
    if (name_id(l, 3, k->attrs[i].ns, k->attrs[i].name, &aid) || event(l, 24 + 2 * (uint32_t)aid) || str(l, k->attrs[i].value)) return -1;
  if (event(l, 23 + 2 * (uint32_t)eid) || (k->text && (event(l, 6) || str(l, k->text)))) return -1;
  for (int i = 0; i < k->nkids; i++)
    if (walk(l, k->kids[i])) return -1;
  return event(l, 4);
}

int enc_lax(enc_t *e, const bim_node_t *k) {
  lw_t l;
  int rc;
  memset(&l, 0, sizeof l);
  bitwriter_init(&l.bw);
  lax_syms_init(&l.sy, e->o->lax_agg);
  rc = walk(&l, k) || event(&l, 7);
  /* ReservedBits, AnyElementLaxCompressionScheme, ReservedBits, Agglutinated, zlib */
  rc = rc || bitwriter_put(e->bw, 0, 14) || bitwriter_put(e->bw, (uint64_t)!!e->o->lax_agg, 1) || bitwriter_put(e->bw, (uint64_t)!!e->o->lax_zlib, 1);
  if (!rc && e->o->lax_zlib) {
    size_t n;
    const unsigned char *in = bitwriter_data(&l.bw, &n);
    size_t cap = n + n / 8 + 64;
    unsigned char *out = e->o->deflate ? malloc(cap) : NULL;
    rc = !out || e->o->deflate(in, n, out, &cap) || bitwriter_put_bytes(e->bw, out, cap);
    free(out);
  } else if (!rc) {
    rc = bitwriter_append(e->bw, &l.bw);
  }
  for (int i = 0; i < l.nns; i++) free(l.ns[i]);
  free(l.ns);
  free(l.el);
  free(l.at);
  bitwriter_free(&l.bw);
  return rc;
}
