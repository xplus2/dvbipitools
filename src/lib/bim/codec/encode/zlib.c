/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/aod.h"
#include "lib/bim/codec/encode/priv.h"

/* compressed chunk of BufferSize bytes, placed where decoder needs them 1st */
typedef struct {
  size_t owner;
  unsigned char *data;
  size_t len;
} zchunk_t;

struct enc_zplan {
  char **s;
  size_t ns;
  size_t cs;
  zchunk_t *c;
  size_t nc;
  size_t next;
};

/* TS 102 822-3-2 4.4.2.4.4, ISO 23001-1 8.7: string as zlib chunks of at most BufferSize */
int enc_zchunks(enc_t *e, const char *v) {
  size_t n = strlen(v) + 1;
  size_t cap = ZLIB_CHUNK_MAX(e->zsize);
  unsigned char *out = e->o->deflate ? malloc(cap) : NULL;
  int rc = out ? 0 : -1;
  for (size_t off = 0; !rc && off < n;) {
    size_t take = n - off < e->zsize ? n - off : e->zsize;
    size_t ol = cap;
    rc = e->o->deflate((const unsigned char *)v + off, take, out, &ol) || !ol || bitwriter_put_vluimsbf8(e->bw, ol) || bitwriter_put_bytes(e->bw, out, ol) ? -1 : 0;
    off += take;
  }
  free(out);
  return rc;
}

size_t enc_zprogress(const enc_t *e) { return e->zmode == 1 ? e->zp->ns : e->zmode == 2 ? e->zi : 0; }

int enc_zstring(enc_t *e, const char *v) {
  struct enc_zplan *p = e->zp;
  if (e->zmode == 0) return enc_zchunks(e, v);
  if (e->zmode == 1) {
    if (p->ns == p->cs) {
      size_t nc = p->cs ? p->cs * 2 : 16;
      char **n = realloc(p->s, nc * sizeof *n);
      if (!n) return -1;
      p->s = n;
      p->cs = nc;
    }
    return (p->s[p->ns++] = strdup(v)) ? 0 : -1;
  }
  if (e->zi >= p->ns) return -1;
  for (; p->next < p->nc && p->c[p->next].owner == e->zi; p->next++)
    if (bitwriter_put_vluimsbf8(e->bw, p->c[p->next].len) || bitwriter_put_bytes(e->bw, p->c[p->next].data, p->c[p->next].len)) return -1;
  e->zi++;
  return 0;
}

static void plan_free(struct enc_zplan *p) {
  for (size_t i = 0; i < p->ns; i++) free(p->s[i]);
  for (size_t i = 0; i < p->nc; i++) free(p->c[i].data);
  free(p->s);
  free(p->c);
}

/* pieces of BufferSize over all strings + their terminators, owner: string holding 1st byte */
static int plan_build(enc_t *e, struct enc_zplan *p) {
  size_t total = 0;
  size_t cap = ZLIB_CHUNK_MAX(e->zsize);
  unsigned char *b;
  unsigned char *out;
  size_t owner = 0;
  size_t owner_end;
  int rc = -1;
  for (size_t i = 0; i < p->ns; i++) total += strlen(p->s[i]) + 1;
  b = malloc(total);
  out = e->o->deflate ? malloc(cap) : NULL;
  p->c = calloc(total / e->zsize + 1, sizeof *p->c);
  if (!b || !out || !p->c) goto done;
  for (size_t i = 0, o = 0; i < p->ns; i++) {
    size_t l = strlen(p->s[i]) + 1;
    memcpy(b + o, p->s[i], l);
    o += l;
  }
  owner_end = strlen(p->s[0]) + 1;
  for (size_t start = 0; start < total; start += e->zsize) {
    size_t take = total - start < e->zsize ? total - start : e->zsize;
    size_t ol = cap;
    while (start >= owner_end) owner_end += strlen(p->s[++owner]) + 1;
    if (e->o->deflate(b + start, take, out, &ol) || !ol || !(p->c[p->nc].data = malloc(ol))) goto done;
    memcpy(p->c[p->nc].data, out, ol);
    p->c[p->nc].len = ol;
    p->c[p->nc++].owner = owner;
  }
  rc = 0;
done:
  free(b);
  free(out);
  return rc;
}

int enc_gathered(enc_t *e, const bim_type_t *t, const bim_node_t *root) {
  bitwriter_t scratch;
  strrepo_writer_t ss;
  enc_t p = *e;
  struct enc_zplan plan;
  int *counters = NULL;
  int rc;
  memset(&plan, 0, sizeof plan);
  if (e->o->aod && e->o->aod->ninsts && !(counters = malloc((size_t)e->o->aod->ninsts * sizeof *counters))) return -1;
  for (int i = 0; counters && i < e->o->aod->ninsts; i++) counters[i] = e->o->aod->insts[i].counter;
  bitwriter_init(&scratch);
  if (strrepo_writer_init(&ss)) {
    free(counters);
    return -1;
  }
  p.bw = &scratch;
  p.sw = &ss;
  p.resync = 0;
  memset(&p.st, 0, sizeof p.st);
  p.st.cs = e->st.cs;
  p.zmode = 1;
  p.zp = &plan;
  rc = enc_element(&p, NULL, t, root, 1);
  for (int i = 0; counters && i < e->o->aod->ninsts; i++) e->o->aod->insts[i].counter = counters[i];
  if (!rc && plan.ns) rc = plan_build(e, &plan);
  if (!rc) {
    e->zmode = 2;
    e->zp = &plan;
    e->zi = 0;
    rc = enc_element(e, NULL, t, root, 1) || e->zi != plan.ns ? -1 : 0;
    e->zmode = 0;
    e->zp = NULL;
  }
  plan_free(&plan);
  free(counters);
  bitwriter_free(&scratch);
  strrepo_writer_free(&ss);
  return rc;
}
