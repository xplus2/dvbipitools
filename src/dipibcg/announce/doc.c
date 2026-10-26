/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/fileutil.h"
#include "dipibcg/announce.h"
#include "lib/bim/bcgau.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/announce/announce_int.h"

/* later channels of a duplicate id dropped. 0 ok, -1 oom */
static int drop_dup_channels(bcg_doc_t *d, int startup) {
  bcg_key_idx_t *ix;
  char *drop;
  int w = 0;
  if (d->channel_count < 2) return 0;
  ix = malloc(sizeof *ix * (size_t)d->channel_count);
  drop = calloc((size_t)d->channel_count, 1);
  if (!ix || !drop) {
    free(ix);
    free(drop);
    log_code(startup ? "GS500101" : "GE500101", "out of memory");
    return -1;
  }
  for (int i = 0; i < d->channel_count; i++) ix[i] = (bcg_key_idx_t){d->channels[i].id, i};
  bcg_key_index_sort(ix, d->channel_count);
  for (int i = 1; i < d->channel_count; i++)
    if (!strcmp(ix[i].key, ix[i - 1].key)) drop[ix[i].idx] = 1;
  for (int i = 0; i < d->channel_count; i++) {
    if (drop[i]) {
      log_code(startup ? "GA000774" : "GW000774", "xmltv: duplicate channel id %s, first wins", d->channels[i].id);
      continue;
    }
    if (w != i) d->channels[w] = d->channels[i];
    w++;
  }
  d->channel_count = w;
  free(ix);
  free(drop);
  return 0;
}

int load_doc(const config_t *cfg, int startup, bcg_doc_t *out) {
  mapping_t map;
  FILE *in;

  in = fileutil_open_std(cfg->input_path, "r");
  if (!in) {
    log_code("GE000100", "cannot open %s", cfg->input_path);
    return -1;
  }
  bcg_doc_init(out);
  if (xmltv_read(in, out)) {
    if (in != stdin) fclose(in);
    bcg_doc_free(out);
    return -1;
  }
  if (in != stdin) fclose(in);
  if (drop_dup_channels(out, startup) || mapping_load(cfg->map_path, &map)) {
    bcg_doc_free(out);
    return -1;
  }
  for (int i = 0; i < out->channel_count; i++) {
    bcg_channel_t *c = &out->channels[i];
    char uri[BCG_ID_LEN];
    unsigned tsid;
    unsigned onid;
    unsigned sid;
    if (!mapping_lookup(&map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
      bufcpy(c->uri, sizeof c->uri, uri);
      c->tsid = tsid;
      c->onid = onid;
      c->sid = sid;
    } else {
      log_code(startup ? "GA000778" : "GW000778", "channel %s not in %s, not announced", c->id, cfg->map_path);
    }
  }
  mapping_free(&map);
  return 0;
}

/* content copied on change, previous last returned. NULL unchanged, oom or none before */
static unsigned char *version_set(bcg_version_t *v, const unsigned char *cont, size_t cont_len, unsigned wrap) {
  unsigned char *old = v->last;
  unsigned char *copy;
  if (v->last && v->last_len == cont_len && !memcmp(v->last, cont, cont_len)) return NULL;
  copy = malloc(cont_len ? cont_len : 1);
  if (!copy) {
    build_log("GE500233", "out of memory, version not advanced");
    return NULL;
  }
  v->version = (v->version + 1) % wrap;
  v->unsent = 1;
  memcpy(copy, cont, cont_len);
  v->last = copy;
  v->last_len = cont_len;
  return old;
}

void version_track(bcg_version_t *v, const unsigned char *cont, size_t cont_len) { free(version_set(v, cont, cont_len, 256)); }

void version_stage(bcg_version_t *v, const unsigned char *cont, size_t cont_len) { (void)version_set(v, cont, cont_len, 256); }

/* insertion position of key, *found set when present */
static int frag_find(const bcg_announce_ctx_t *ctx, const char *key, int *found) {
  int lo = 0;
  int hi = ctx->nfrags;
  *found = 0;
  while (lo < hi) {
    int mid = lo + (hi - lo) / 2;
    int c = strcmp(ctx->frags[mid].key, key);
    if (!c) {
      *found = 1;
      return mid;
    }
    if (c < 0) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

static frag_state_t *frag_get(bcg_announce_ctx_t *ctx, const char *base) {
  char key[sizeof ctx->frags[0].key];
  int found;
  int pos;
  unsigned dup = 1;
  for (;;) {
    sbuf_t k;
    sbuf_init(&k, key, sizeof key);
    sbuf_add(&k, base);
    if (dup > 1) {
      sbuf_add(&k, "#");
      sbuf_add_uint(&k, dup);
    }
    if (k.truncated) return NULL;
    pos = frag_find(ctx, key, &found);
    if (!found || ctx->frags[pos].gen != ctx->frag_gen) break;
    dup++; /* same key twice in one build: distinct fragment */
  }
  if (!found) {
    void *np;
    if (ctx->frag_next >= 0xFFFFFFu) return NULL;
    np = array_grow(ctx->frags, &ctx->frags_cap, ctx->nfrags + 1, sizeof *ctx->frags);
    if (!np) return NULL;
    ctx->frags = np;
    memmove(&ctx->frags[pos + 1], &ctx->frags[pos], sizeof *ctx->frags * (size_t)(ctx->nfrags - pos));
    memset(&ctx->frags[pos], 0, sizeof ctx->frags[pos]);
    bufcpy(ctx->frags[pos].key, sizeof ctx->frags[pos].key, key);
    ctx->frags[pos].id = ++ctx->frag_next;
    ctx->nfrags++;
  }
  ctx->frags[pos].gen = ctx->frag_gen;
  return &ctx->frags[pos];
}

static int ref_id_cmp(const void *a, const void *b) {
  uint32_t x = ((const bcgau_ref_t *)a)->frag_id;
  uint32_t y = ((const bcgau_ref_t *)b)->frag_id;
  return (x > y) - (x < y);
}

typedef struct {
  const bcg_doc_t *doc;
  const bcgau_ref_t *refs;
} doc_keys_t;

static int key_doc(const void *kctx, int i, char *buf, size_t cap) {
  const doc_keys_t *k = kctx;
  char crid[TVA_CRID_BUF];
  sbuf_t b;
  sbuf_init(&b, buf, cap);
  if (k->refs[i].kind == BCGAU_PROGRAM) {
    sbuf_add(&b, "P:");
    sbuf_add(&b, tva_programme_crid(&k->doc->programmes[k->refs[i].index], crid, sizeof crid));
  } else {
    sbuf_add(&b, k->refs[i].kind == BCGAU_SCHEDULE ? "S:" : "I:");
    sbuf_add(&b, k->doc->channels[k->refs[i].index].id);
  }
  return b.truncated;
}

/* type code + first attribute value, names ProgramInformation, Schedule + ServiceInformation */
int key_node(const void *kctx, int i, char *buf, size_t cap) {
  const dvbtva_fuu_t *f = (const dvbtva_fuu_t *)kctx + i;
  sbuf_t b;
  sbuf_init(&b, buf, cap);
  sbuf_add(&b, "T:");
  sbuf_add_uint(&b, f->code);
  sbuf_add(&b, ":");
  if (f->root->nattrs) sbuf_add(&b, f->root->attrs[0].value);
  return b.truncated;
}

int frag_assign_keys(bcg_announce_ctx_t *ctx, frag_key_fn key, const void *kctx, const unsigned char *au, size_t au_len, const unsigned char *strs, size_t strs_len, bcgau_ref_t *refs, int n) {
  for (int i = 0; i < n; i++) {
    char base[TVA_CRID_BUF + 4];
    size_t a0 = refs[i].au_off;
    size_t a1 = i + 1 < n ? refs[i + 1].au_off : au_len;
    size_t s0 = refs[i].str_off;
    size_t s1 = i + 1 < n ? refs[i + 1].str_off : strs_len;
    size_t len = (a1 - a0) + (s1 - s0);
    frag_state_t *f = key(kctx, i, base, sizeof base) ? NULL : frag_get(ctx, base);
    if (!f) return -1;
    if (len > (size_t)ctx->fbuf_cap) {
      void *np = array_grow(ctx->fbuf, &ctx->fbuf_cap, (int)len, 1);
      if (!np) return -1;
      ctx->fbuf = np;
    }
    memcpy(ctx->fbuf, au + a0, a1 - a0);
    memcpy(ctx->fbuf + (a1 - a0), strs + s0, s1 - s0);
    /* TS 102 822-3-2 4.6.1.1: fragment_version mod 255 */
    free(version_set(&f->ver, ctx->fbuf, len, 255));
    refs[i].frag_id = f->id;
    refs[i].version = (uint8_t)f->ver.version;
  }
  qsort(refs, (size_t)n, sizeof *refs, ref_id_cmp);
  return 0;
}

int frag_touch_keys(bcg_announce_ctx_t *ctx, frag_key_fn key, const void *kctx, bcgau_ref_t *refs, int n) {
  for (int i = 0; i < n; i++) {
    char base[TVA_CRID_BUF + 4];
    const frag_state_t *f = key(kctx, i, base, sizeof base) ? NULL : frag_get(ctx, base);
    if (!f) return -1;
    refs[i].frag_id = f->id;
    refs[i].version = (uint8_t)f->ver.version;
  }
  return 0;
}

int frag_touch(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, bcgau_ref_t *refs, int n) {
  doc_keys_t k = {doc, refs};
  return frag_touch_keys(ctx, key_doc, &k, refs, n);
}

int frag_assign(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, const unsigned char *au, size_t au_len, const unsigned char *strs, size_t strs_len, bcgau_ref_t *refs, int n) {
  doc_keys_t k = {doc, refs};
  return frag_assign_keys(ctx, key_doc, &k, au, au_len, strs, strs_len, refs, n);
}

void frag_prune(bcg_announce_ctx_t *ctx) {
  int w = 0;
  for (int i = 0; i < ctx->nfrags; i++) {
    if (ctx->frags[i].gen == ctx->frag_gen) ctx->frags[w++] = ctx->frags[i];
    else free(ctx->frags[i].ver.last);
  }
  ctx->nfrags = w;
}

void frag_free(bcg_announce_ctx_t *ctx) {
  for (int i = 0; i < ctx->nfrags; i++) free(ctx->frags[i].ver.last);
  free(ctx->frags);
  free(ctx->fbuf);
  ctx->frags = NULL;
  ctx->fbuf = NULL;
  ctx->fbuf_cap = 0;
  ctx->frags_cap = ctx->fbuf_cap;
  ctx->nfrags = ctx->frags_cap;
}
