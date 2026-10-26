/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/decode/priv.h"
#include "lib/bim/codec/laxsym.h"

typedef struct {
  int ns;
  char *name;
  const char *kname; /* kept on node, set on first use */
  const char *kns;
} lax_name_t;

typedef struct {
  dec_t *d;
  bitreader_t *br;
  lax_syms_t sy;
  uint32_t fifo[3];
  int nfifo;
  int fpos;
  char **ns;
  int nns;
  lax_name_t *el;
  lax_name_t *at;
  int nel;
  int nat;
  dstrbuf_t sb; /* string being read, reused */
} lax_t;

static int lax_byte(lax_t *l, unsigned *b) {
  uint64_t v;
  if (bitreader_get(l->br, 8, &v)) return -1;
  *b = (unsigned)v;
  return 0;
}

static int lax_event(lax_t *l, uint32_t *e) {
  if (l->fpos == l->nfifo) {
    uint64_t ext;
    uint32_t evs[3];
    int n;
    if (bitreader_get(l->br, 1, &ext)) return -1;
    if (ext) {
      uint64_t uid;
      if (bitreader_get(l->br, 15, &uid)) return -1;
      evs[0] = (uint32_t)uid;
      n = 1;
    } else {
      uint64_t code;
      const lax_sym_t *sym;
      if (bitreader_get(l->br, 7, &code)) return -1;
      sym = lax_syms_by_code(&l->sy, (int)code);
      if (!sym) return -1;
      n = sym->n;
      memcpy(evs, sym->ev, (size_t)n * sizeof evs[0]);
    }
    for (int i = 0; i < n; i++) lax_syms_event(&l->sy, evs[i]);
    memcpy(l->fifo, evs, (size_t)n * sizeof evs[0]);
    l->nfifo = n;
    l->fpos = 0;
  }
  *e = l->fifo[l->fpos++];
  return 0;
}

/* zero terminated UTF-8 */
static int lax_string(lax_t *l, char **out) {
  if (!l->sb.buf) dstrbuf_init(&l->sb);
  if (!l->sb.buf) return -1;
  l->sb.len = 0;
  l->sb.buf[0] = '\0';
  for (;;) {
    unsigned b;
    char c;
    if (lax_byte(l, &b)) return -1;
    if (!b) break;
    c = (char)b;
    if (dec_add_n(&l->sb, &c, 1)) return -1;
  }
  if (!(*out = malloc(l->sb.len + 1))) return -1;
  memcpy(*out, l->sb.buf, l->sb.len + 1);
  return 0;
}

static int lax_strings(lax_t *l, int n) {
  for (int i = 0; i < n; i++) {
    char *s;
    if (lax_string(l, &s)) return -1;
    free(s);
  }
  return 0;
}

static int lax_nsid(lax_t *l, int *id) {
  unsigned b;
  if (lax_byte(l, &b) || (int)b >= l->nns) return -1;
  *id = (int)b;
  return 0;
}

static void lax_free(lax_t *l) {
  for (int i = 0; i < l->nns; i++) free(l->ns[i]);
  for (int i = 0; i < l->nel; i++) free(l->el[i].name);
  for (int i = 0; i < l->nat; i++) free(l->at[i].name);
  free(l->ns);
  free(l->el);
  free(l->at);
  free(l->sb.buf);
}

static int lax_events(dec_t *d, bitreader_t *br, int agg, bim_node_t *node);

/* name and ns of entry i kept on node once, ns "" none */
static int keep_name(lax_t *l, bim_node_t *node, int el, int i, const char **name, const char **ns) {
  lax_name_t *e = el ? &l->el[i] : &l->at[i];
  const char *uri = l->ns[e->ns];
  if (!e->kname) {
    e->kname = bim_node_keep(node, e->name);
    if (!e->kname) return -1;
  }
  if (*uri && !e->kns) {
    e->kns = bim_node_keep(node, uri);
    if (!e->kns) return -1;
  }
  *name = e->kname;
  *ns = *uri ? e->kns : NULL;
  return 0;
}

/* Amd 2 7.5.2.4.5.4: events to elements below node. bits: rest of AnyElement */
int dec_lax(dec_t *d, bitreader_t *br, size_t bits, bim_node_t *node) {
  uint64_t v;
  uint64_t agg;
  uint64_t z;
  size_t start = bitreader_bits_left(br);
  unsigned char *in;
  unsigned char *out = NULL;
  size_t n;
  bitreader_t zbr;
  int rc = -1;
  /* ReservedBits, AnyElementLaxCompressionScheme, ReservedBits, Agglutinated, zlib */
  if (bitreader_get(br, 4, &v) || bitreader_get(br, 4, &v) || (v != 0 && v != 1)) return -1;
  if (bitreader_get(br, 6, &v) || bitreader_get(br, 1, &agg) || bitreader_get(br, 1, &z)) return -1;
  if (!z) return lax_events(d, br, (int)agg, node);
  /* symbol bytes after header zlib compressed */
  if (!d->inflate || start - bitreader_bits_left(br) > bits) return -1;
  n = (bits - (start - bitreader_bits_left(br))) / 8;
  if (!(in = malloc(n ? n : 1)) || bitreader_get_bytes(br, in, n)) {
    free(in);
    return -1;
  }
  for (size_t cap = 4096; cap <= ((size_t)1 << 24); cap *= 4) {
    size_t outlen = cap;
    unsigned char *o = realloc(out, cap);
    if (!o) break;
    out = o;
    if (!d->inflate(in, n, out, &outlen)) {
      bitreader_init(&zbr, out, outlen);
      rc = lax_events(d, &zbr, (int)agg, node);
      break;
    }
  }
  free(in);
  free(out);
  return rc;
}

static int lax_events(dec_t *d, bitreader_t *br, int agg, bim_node_t *node) {
  lax_t l;
  bim_node_t *stack[64];
  int depth = 0;
  struct {
    int id;
    char *v;
  } pend[64];
  int npend = 0;
  int rc = -1;
  memset(&l, 0, sizeof l);
  l.d = d;
  l.br = br;
  lax_syms_init(&l.sy, agg);
  stack[depth++] = node;
  for (;;) {
    uint32_t e;
    char *s = NULL;
    int id;
    if (lax_event(&l, &e)) goto done;
    switch (e) {
      case 0: goto done; /* BiM Encoding: no type for 7.3 payload */
      case 1: {
        char **n;
        if (l.nns >= 256) goto done;
        n = realloc(l.ns, sizeof *n * (size_t)(l.nns + 1));
        if (!n) goto done;
        l.ns = n;
        if (lax_string(&l, &l.ns[l.nns])) goto done;
        l.nns++;
        break;
      }
      case 2:
      case 3: {
        void *n;
        if (lax_nsid(&l, &id) || lax_string(&l, &s)) goto done;
        n = realloc(e == 2 ? (void *)l.el : (void *)l.at, sizeof *l.el * (size_t)((e == 2 ? l.nel : l.nat) + 1));
        if (!n) {
          free(s);
          goto done;
        }
        if (e == 2) {
          l.el = n;
          l.el[l.nel].ns = id;
          l.el[l.nel].kns = NULL;
          l.el[l.nel].kname = l.el[l.nel].kns;
          l.el[l.nel++].name = s;
        } else {
          l.at = n;
          l.at[l.nat].ns = id;
          l.at[l.nat].kns = NULL;
          l.at[l.nat].kname = l.at[l.nat].kns;
          l.at[l.nat++].name = s;
        }
        break;
      }
      case 4:
        if (depth < 2) goto done;
        depth--;
        break;
      case 5:
        if (lax_nsid(&l, &id) || lax_strings(&l, 1)) goto done;
        break;
      case 6: {
        bim_node_t *k;
        if (lax_string(&l, &s)) goto done;
        if (!(k = bim_node_new(NULL)) || bim_node_add(stack[depth - 1], k)) {
          free(s);
          bim_node_free(k);
          goto done;
        }
        k->text = s;
        break;
      }
      case 7: rc = depth == 1 ? 0 : -1; goto done;
      case 8: if (lax_strings(&l, 2)) goto done; break;
      case 9: case 21: case 22: case 16: if (lax_strings(&l, 1)) goto done; break;
      case 10: case 14: case 19: if (lax_strings(&l, 3)) goto done; break;
      case 11: if (lax_strings(&l, 4)) goto done; break;
      case 12: if (lax_strings(&l, 5)) goto done; break;
      case 13: case 15: if (lax_strings(&l, 2)) goto done; break;
      case 17: case 18: case 20: break;
      default:
        if (e < 23) goto done;
        if (e % 2) {
          int eid = (int)(e - 23) / 2;
          bim_node_t *k;
          const char *nm;
          const char *ns;
          if (eid >= l.nel || depth >= 64) goto done;
          if (keep_name(&l, node, 1, eid, &nm, &ns)) goto done;
          k = bim_node_new(nm);
          if (!k) goto done;
          k->ns = ns;
          if (bim_node_add(stack[depth - 1], k)) {
            bim_node_free(k);
            goto done;
          }
          for (int i = 0; i < npend; i++) {
            const char *an;
            const char *as;
            char *v = pend[i].v;
            if (keep_name(&l, node, 0, pend[i].id, &an, &as)) goto done;
            pend[i].v = NULL;
            if (bim_node_add_attr_ns(k, as, an, v)) goto done;
          }
          npend = 0;
          stack[depth++] = k;
        } else {
          int aid = (int)(e - 24) / 2;
          if (aid >= l.nat || npend >= 64 || lax_string(&l, &pend[npend].v)) goto done;
          pend[npend++].id = aid;
        }
    }
  }
done:
  for (int i = 0; i < npend; i++) free(pend[i].v);
  lax_free(&l);
  return rc;
}
