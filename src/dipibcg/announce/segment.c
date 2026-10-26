/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "lib/bim/bcgip.h"
#include "lib/bim/container.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/tva/bcg_doc.h"
#include "dipibcg/announce/announce_int.h"

int build_segment(bitwriter_t *bw, const strrepo_writer_t *sw, const bcgau_ref_t *refs, int nrefs, bcg_version_t *ver, int compress, seg_cache_t *seg) {
  size_t bits_len;
  size_t strs_len;
  size_t cont_len;
  bim_fragref_t *fr = NULL;
  const unsigned char *bits = bitwriter_data(bw, &bits_len);
  const unsigned char *strs = strrepo_writer_data(sw, &strs_len);
  unsigned char *cont;
  unsigned char *wrapped;
  size_t wrapped_len;
  int rc = SEG_FAILED;

  if (nrefs > 0 && !(fr = malloc(sizeof *fr * (size_t)nrefs))) {
    build_log("GE100141", "container build failed");
    return SEG_NO_DOC;
  }
  for (int i = 0; i < nrefs; i++) fr[i] = (bim_fragref_t){.au_off = refs[i].au_off, .str_off = refs[i].str_off, .frag_id = refs[i].frag_id, .version = refs[i].version};
  if (bim_container_build(BIM_FRAGREF_DVB_BIM, bits, bits_len, strs, strs_len, fr, nrefs, &cont, &cont_len) != 0) {
    free(fr);
    build_log("GE100141", "container build failed");
    return SEG_NO_DOC;
  }
  free(fr);
  version_stage(ver, cont, cont_len);
  if (bim_bcg_wrap(cont, cont_len, compress ? BCG_DEFLATE : NULL, &wrapped, &wrapped_len) != 0) {
    build_log("GE100142", "wrapper build failed");
  } else if (wrapped_len > DVBSTP_MAX_SEGMENT) {
    build_log("GE100143", "guide too large (%zu bytes, max %u), reduce window or channels", wrapped_len, DVBSTP_MAX_SEGMENT);
    free(wrapped);
  } else {
    seg->buf = wrapped;
    seg->len = wrapped_len;
    rc = SEG_OK;
  }
  free(cont);
  return rc;
}

int build_plain(const char *xml, size_t len, bcg_version_t *ver, seg_cache_t *seg) {
  if (len > DVBSTP_MAX_SEGMENT) {
    build_log("GE100143", "guide too large (%zu bytes, max %u), reduce window or channels", len, DVBSTP_MAX_SEGMENT);
    return SEG_FAILED;
  }
  version_stage(ver, (const unsigned char *)xml, len);
  if (!(seg->buf = malloc(len ? len : 1))) return SEG_NO_DOC;
  memcpy(seg->buf, xml, len);
  seg->len = len;
  return SEG_OK;
}

static int send_payload(mcast_t *m, unsigned compr, unsigned payload_id, unsigned segment_id, unsigned version, const unsigned char *data, size_t len, int64_t budget_ns) {
  if (dvbstp_send_segment(m, &(dvbstp_send_t){.payload_id = payload_id, .segment_id = segment_id, .segment_version = version, .compr = compr, .want_crc = 1, .pacing_budget_ns = budget_ns}, data, len) == 0)
    return 0;
  log_code("GE100144", "send failed");
  return -1;
}

int send_segment(mcast_t *m, unsigned compr, const seg_cache_t *seg, unsigned version, unsigned segment_id, int64_t budget_ns) {
  return send_payload(m, compr, DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, segment_id, version, seg->buf, seg->len, budget_ns);
}

/* TS 102 539 7.3, segment_id operator (TS 102 034 5.2.12.23) */
int send_tva_init(mcast_t *m, const bcg_schema_t *s, unsigned version) { return send_payload(m, 1, DVBSTP_PAYLOAD_BCG_TVA_INIT, 0, version, s->init, s->init_len, 0); }

typedef struct {
  int *b;
  int ng;
  size_t au;
  size_t str;
  size_t nfuu;
} plan_t;

/* unit c joins running group. New group if per_unit or over BIM_REPO_MAX. -1 too many segments */
static int plan_unit(plan_t *p, int c, size_t au_body, size_t str_body, size_t nf, int per_unit) {
  if (c > 0 && (per_unit || (size_t)bitwriter_vluimsbf8_len(p->nfuu + nf) + p->au + au_body > BIM_REPO_MAX || 1 + p->str + str_body > BIM_REPO_MAX)) {
    if ((unsigned)p->ng + 1 >= BCG_MAX_SEGMENTS) {
      build_log("GE100147", "guide needs more than %u segments, reduce channels", BCG_MAX_SEGMENTS);
      return -1;
    }
    p->b[++p->ng] = c;
    p->au = p->str = p->nfuu = 0;
  }
  p->au += au_body;
  p->str += str_body;
  p->nfuu += nf;
  return 0;
}

/* au_body: access unit bytes less fuu count. str_body: repository less encoding byte. -1 over BIM_REPO_MAX */
static int body_sizes(bitwriter_t *bw, strrepo_writer_t *sw, size_t nf, size_t *au_body, size_t *str_body) {
  size_t au_len;
  size_t str_len;
  bitwriter_data(bw, &au_len);
  strrepo_writer_data(sw, &str_len);
  *au_body = au_len - (size_t)bitwriter_vluimsbf8_len((uint64_t)nf);
  *str_body = str_len - 1;
  return (size_t)bitwriter_vluimsbf8_len((uint64_t)nf) + *au_body > BIM_REPO_MAX || 1 + *str_body > BIM_REPO_MAX ? -1 : 0;
}

int segment_fits(bitwriter_t *bw, strrepo_writer_t *sw, size_t nfuu) {
  size_t au_body;
  size_t str_body;
  return !body_sizes(bw, sw, nfuu, &au_body, &str_body);
}

/* container groups: channels, or fragments if f given. bounds[g]..bounds[g+1] */
static int plan_bounds(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, unsigned au_flags, const dvbtva_fuu_t *f, int n, int **bounds, int *ngroups) {
  plan_t p = {.b = malloc(sizeof *p.b * ((size_t)n + 2))};
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc = -1;

  if (!p.b) return -1;
  p.b[0] = 0;
  if (!f && n <= (int)BCG_MAX_SEGMENTS) {
    /* container per channel, size checked when built */
    for (int c = 1; c <= n; c++) p.b[c] = c;
    *ngroups = n ? n : 1;
    p.b[*ngroups] = n;
    *bounds = p.b;
    return 0;
  }
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) goto done;
  for (int c = 0; c < n; c++) {
    size_t au_body;
    size_t str_body;
    int nf = 1;
    bitwriter_reset(&bw);
    strrepo_writer_free(&sw);
    if (strrepo_writer_init(&sw)) goto done;
    if (f ? bcgau_write(sc, a, &f[c], 1, NULL, &bw, &sw) : bcgau_encode_range(sc, a, doc, ix, c, c + 1, au_flags, &bw, &sw, &nf, NULL)) goto done;
    if (body_sizes(&bw, &sw, (size_t)nf, &au_body, &str_body)) {
      if (f) build_log("GE100152", "fragment %d alone exceeds %u bytes per repository", c, BIM_REPO_MAX);
      else build_log("GE100145", "channel %s alone exceeds %u bytes per repository, reduce its programmes", doc->channels[c].id, BIM_REPO_MAX);
      goto done;
    }
    if (plan_unit(&p, c, au_body, str_body, (size_t)nf, 0)) goto done;
  }
  p.b[++p.ng] = n;
  *bounds = p.b;
  *ngroups = p.ng;
  p.b = NULL;
  rc = 0;
done:
  free(p.b);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

int plan_groups(bcgau_scratch_t *sc, const bcgau_t *a, const bcg_doc_t *doc, const bcg_doc_index_t *ix, unsigned au_flags, int **bounds, int *ngroups) {
  return plan_bounds(sc, a, doc, ix, au_flags, NULL, doc->channel_count, bounds, ngroups);
}

int plan_groups_frags(bcgau_scratch_t *sc, const bcgau_t *a, const dvbtva_fuu_t *f, int n, int **bounds, int *ngroups) {
  return plan_bounds(sc, a, NULL, NULL, 0, f, n, bounds, ngroups);
}

static int main_plain(bcg_announce_ctx_t *ctx, bcg_out_t *st, const bcg_doc_t *d) {
  bcgau_frags_t fr;
  char *xml = NULL;
  size_t xl = 0;
  int rc = -1;
  if (!bcgau_frags_tva_main(&ctx->sch->au, d, &fr)) {
    if (!bcgau_xml(&ctx->sch->au, fr.f, fr.n, &xml, &xl)) rc = build_plain(xml, xl, &st->main_ver, &st->main_seg) == SEG_OK ? 0 : -1;
    bcgau_frags_free(&fr);
  }
  free(xml);
  return rc;
}

static int key_main(const void *kctx, int i, char *buf, size_t cap) {
  sbuf_t b;
  (void)kctx;
  (void)i;
  sbuf_init(&b, buf, cap);
  sbuf_add(&b, "M:");
  return b.truncated;
}

int build_tva_main(bcg_announce_ctx_t *ctx, bcg_out_t *st) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bcg_doc_t d;
  int rc = -1;
  if (!ctx->cfg->publisher) return 0;
  st->main_sch = ctx->sch;
  if (ctx->out.main_seg.buf && ctx->out.main_sch == ctx->sch) {
    /* constant per schema: previous bytes, fragment kept alive */
    const seg_cache_t *old = &ctx->out.main_seg;
    if (ctx->cfg->encoding != ENC_PLAIN && (array_reserve((void **)&ctx->sc.refs, &ctx->sc.refs_cap, 1, sizeof *ctx->sc.refs) || frag_touch_keys(ctx, key_main, NULL, ctx->sc.refs, 1)))
      return -1;
    if (!(st->main_seg.buf = malloc(old->len ? old->len : 1))) return -1;
    memcpy(st->main_seg.buf, old->buf, old->len);
    st->main_seg.len = old->len;
    return 0;
  }
  memset(&d, 0, sizeof d);
  bufcpy(d.publisher, sizeof d.publisher, ctx->cfg->publisher);
  bufcpy(d.lang, sizeof d.lang, ctx->cfg->lang ? ctx->cfg->lang : "");
  if (ctx->cfg->encoding == ENC_PLAIN) {
    rc = main_plain(ctx, st, &d);
    if (rc) build_log("GE100150", "TVAMain build failed (publisher %s, lang %s)", d.publisher, d.lang[0] ? d.lang : "none");
    return rc;
  }
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) {
    bitwriter_free(&bw);
    return -1;
  }
  if (bcgau_encode_tva_main(&ctx->sc, &ctx->sch->au, &d, &bw, &sw)) {
    build_log("GE100150", "TVAMain build failed (publisher %s, lang %s)", d.publisher, d.lang[0] ? d.lang : "none");
  } else {
    size_t aul;
    size_t srl;
    const unsigned char *aud = bitwriter_data(&bw, &aul);
    const unsigned char *srd = strrepo_writer_data(&sw, &srl);
    if (!frag_assign_keys(ctx, key_main, NULL, aud, aul, srd, srl, ctx->sc.refs, 1))
      rc = build_segment(&bw, &sw, ctx->sc.refs, 1, &st->main_ver, ctx->cfg->compress, &st->main_seg) == SEG_OK ? 0 : -1;
  }
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

/* key copy owned by ctx, NULL on oom */
static const char *keep(bcg_announce_ctx_t *ctx, const char *s) {
  char *c;
  if (ctx->ix_nkeys >= ctx->ix_keys_cap) {
    void *np = array_grow(ctx->ix_keys, &ctx->ix_keys_cap, ctx->ix_nkeys + 1, sizeof *ctx->ix_keys);
    if (!np) return NULL;
    ctx->ix_keys = np;
  }
  c = strdup(s);
  if (c) ctx->ix_keys[ctx->ix_nkeys++] = c;
  return c;
}

static int add_key(bcg_announce_ctx_t *ctx, int kind, const char *k0, const char *k1, unsigned container, uint32_t frag) {
  const char *a = keep(ctx, k0);
  const char *b = k1 ? keep(ctx, k1) : NULL;
  if (!a || (k1 && !b)) return -1;
  return bim_index_add(&ctx->ix_in[kind], a, b, (uint16_t)container, frag);
}

int index_collect(bcg_announce_ctx_t *ctx, const bcg_doc_t *doc, const bcg_doc_index_t *ix, const bcgau_ref_t *refs, int n, unsigned container) {
  unsigned m = ctx->cfg->index_mask;
  if (!m) return 0;
  for (int i = 0; i < n; i++) {
    const bcgau_ref_t *r = &refs[i];
    if (r->kind == BCGAU_PROGRAM) {
      const bcg_programme_t *pr = &doc->programmes[r->index];
      char buf[TVA_CRID_BUF];
      if ((m & 1u << BIM_INDEX_PI_CRID) && add_key(ctx, BIM_INDEX_PI_CRID, tva_programme_crid(pr, buf, sizeof buf), NULL, container, r->frag_id)) return -1;
      if ((m & 1u << BIM_INDEX_PI_TITLE) && pr->title && add_key(ctx, BIM_INDEX_PI_TITLE, pr->title, NULL, container, r->frag_id)) return -1;
    } else if (r->kind == BCGAU_SCHEDULE) {
      const char *ch = doc->channels[r->index].id;
      const char *end = bcgau_schedule_end(doc->programmes, ix->first[r->index], ix->next);
      if ((m & 1u << BIM_INDEX_SCHED_TIME) && end && add_key(ctx, BIM_INDEX_SCHED_TIME, end, ch, container, r->frag_id)) return -1;
      if (!(m & 1u << BIM_INDEX_SCHED_TITLE)) continue;
      for (int p = ix->first[r->index]; p != -1; p = ix->next[p])
        if (doc->programmes[p].title && add_key(ctx, BIM_INDEX_SCHED_TITLE, doc->programmes[p].title, NULL, container, r->frag_id)) return -1;
    }
  }
  return 0;
}

void index_reset(bcg_announce_ctx_t *ctx) {
  for (int k = 0; k < BIM_INDEX_KINDS; k++) ctx->ix_in[k].n = 0;
  for (int i = 0; i < ctx->ix_nkeys; i++) free(ctx->ix_keys[i]);
  ctx->ix_nkeys = 0;
}

static void segs_free(seg_cache_t *s, int n) {
  for (int i = 0; i < n; i++) free(s[i].buf);
  free(s);
}

static void index_drop_segs(bcg_out_t *st) {
  segs_free(st->iseg, st->niseg);
  free(st->iseg_id);
  st->iseg = NULL;
  st->iseg_id = NULL;
  st->niseg = 0;
}

void index_free(bcg_announce_ctx_t *ctx) {
  index_reset(ctx);
  for (int k = 0; k < BIM_INDEX_KINDS; k++) bim_index_input_free(&ctx->ix_in[k]);
  free(ctx->ix_keys);
  ctx->ix_keys = NULL;
  ctx->ix_keys_cap = 0;
}

/* versions by position: [0] list container, then sub-index containers in id order */
int index_build(bcg_announce_ctx_t *ctx, bcg_out_t *st, unsigned first_id) {
  bim_index_cont_t *conts;
  int n;
  int rc;
  index_drop_segs(st);
  if (!ctx->cfg->index_mask) return 0;
  rc = bim_index_build(ctx->cfg->index_mask, ctx->ix_in, (uint16_t)first_id, &conts, &n);
  if (rc) {
    build_log("GE100151", "index build failed (%s)", rc == -2 ? "index list strings above 64 KiB" : rc == -3 ? "container ids exhausted" : "out of memory");
    return -1;
  }
  st->iseg = calloc((size_t)n, sizeof *st->iseg);
  st->iseg_id = calloc((size_t)n, sizeof *st->iseg_id);
  if (!st->iseg || !st->iseg_id || grow_zeroed((void **)&st->ivers, &st->ivers_n, n, sizeof *st->ivers)) goto fail;
  st->niseg = n;
  for (int i = 0; i < n; i++) {
    unsigned char *cont;
    size_t cont_len;
    if (bim_container_build_structs(conts[i].structs, conts[i].nstructs, &cont, &cont_len)) goto fail;
    version_stage(&st->ivers[i], cont, cont_len);
    rc = bim_bcg_wrap(cont, cont_len, ctx->cfg->compress ? BCG_DEFLATE : NULL, &st->iseg[i].buf, &st->iseg[i].len);
    free(cont);
    if (rc) goto fail;
    st->iseg_id[i] = conts[i].id;
  }
  bim_index_conts_free(conts, n);
  return 0;
fail:
  build_log("GE100151", "index build failed (%s)", "container build");
  bim_index_conts_free(conts, n);
  index_drop_segs(st);
  return -1;
}

static int vers_copy(bcg_version_t **dst, int *dn, const bcg_version_t *src, int sn) {
  *dst = NULL;
  *dn = 0;
  if (!sn) return 0;
  if (!(*dst = malloc(sizeof **dst * (size_t)sn))) return -1;
  memcpy(*dst, src, sizeof **dst * (size_t)sn);
  *dn = sn;
  return 0;
}

int out_stage(bcg_out_t *st, const bcg_out_t *base) {
  memset(st, 0, sizeof *st);
  if (vers_copy(&st->vers, &st->vers_n, base->vers, base->vers_n) || vers_copy(&st->ivers, &st->ivers_n, base->ivers, base->ivers_n)) {
    free(st->vers);
    memset(st, 0, sizeof *st);
    return -1;
  }
  st->main_ver = base->main_ver;
  st->init_ver = base->init_ver;
  return 0;
}

static void out_bufs_free(bcg_out_t *o) {
  segs_free(o->seg, o->nseg);
  index_drop_segs(o);
  free(o->main_seg.buf);
  free(o->gsel);
  free(o->goff);
}

/* v's last unless shared with keep */
static void ver_drop(const bcg_version_t *v, const bcg_version_t *keep) {
  if (!keep || v->last != keep->last) free(v->last);
}

static void vers_drop(bcg_version_t *v, int n, const bcg_version_t *keep, int nkeep) {
  for (int i = 0; i < n; i++) ver_drop(&v[i], i < nkeep ? &keep[i] : NULL);
  free(v);
}

void out_commit(bcg_out_t *base, bcg_out_t *st) {
  out_bufs_free(base);
  vers_drop(base->vers, base->vers_n, st->vers, st->vers_n);
  vers_drop(base->ivers, base->ivers_n, st->ivers, st->ivers_n);
  ver_drop(&base->main_ver, &st->main_ver);
  ver_drop(&base->init_ver, &st->init_ver);
  *base = *st;
  memset(st, 0, sizeof *st);
}

void out_discard(bcg_out_t *st, const bcg_out_t *base) {
  out_bufs_free(st);
  vers_drop(st->vers, st->vers_n, base->vers, base->vers_n);
  vers_drop(st->ivers, st->ivers_n, base->ivers, base->ivers_n);
  ver_drop(&st->main_ver, &base->main_ver);
  ver_drop(&st->init_ver, &base->init_ver);
  memset(st, 0, sizeof *st);
}

void out_free(bcg_out_t *o) {
  out_bufs_free(o);
  vers_drop(o->vers, o->vers_n, NULL, 0);
  vers_drop(o->ivers, o->ivers_n, NULL, 0);
  ver_drop(&o->main_ver, NULL);
  ver_drop(&o->init_ver, NULL);
  memset(o, 0, sizeof *o);
}

/* TS 102 539 7.4, segment_id unused */
int send_tva_main(mcast_t *m, unsigned compr, const seg_cache_t *seg, unsigned version) { return send_payload(m, compr, DVBSTP_PAYLOAD_BCG_TVAMAIN, 0, version, seg->buf, seg->len, 0); }

int send_index(mcast_t *m, const seg_cache_t *seg, unsigned version, unsigned container_id, int64_t budget_ns) {
  return send_payload(m, 1, DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER, container_id, version, seg->buf, seg->len, budget_ns);
}
