/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/dash/dash.h"
#include "dipixy/dash/dash_int.h"
#include "lib/sys/ioutil.h"

/* want_ll: route-selected, not derived from snap->part_target. split: video and audio as separate AdaptationSets */
static size_t build_mpd(const mpd_fmt_ctx_t *ctx, char *mpd, size_t cap) {
  const hls_store_t *s = ctx->s;
  const hls_snapshot_t *snap = ctx->snap;
  const hls_snapshot_t *vsnap = ctx->vsnap ? ctx->vsnap : snap;
  int split = ctx->asnap != NULL;
  char *mp = mpd;
  char avail[32];
  char publish[32];
  char codecs[64];
  double min_update;
  double tsb_depth;
  double pres_delay;
  double min_buffer;
  int ll = ctx->want_ll && snap->part_target > 0.0;
  size_t utc_len = xml_escaped_len(ctx->utc_url);
  size_t audio_max = split ? DASH_MPD_AUDIO_MAX + (ll ? utc_len : 0) : 0;

  /* url in tail, LL: also once per AdaptationSet. omit LL timing if it can't fit */
  if (ll && DASH_MPD_HEAD_MAX + audio_max + 2 * utc_len + DASH_MPD_TAIL_MAX > cap) {
    ll = 0;
    audio_max = split ? DASH_MPD_AUDIO_MAX : 0;
  }
  if (DASH_MPD_HEAD_MAX + audio_max + utc_len + DASH_MPD_TAIL_MAX > cap) utc_len = 0;
  mpd_avail_time(s, vsnap, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);

  /* 2 segs slack, not 1. real durations vary */
  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, ll, snap->part_target, mpd_dvb_conformant(ctx));
  mp = write_period_open(mp, vsnap);
  mp = WRITE_LIT(mp, "    <AdaptationSet id=\"1\" contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"");
  mp = write_start_with_sap(mp, &vsnap->video);
  mp = WRITE_LIT(mp, "\">\n");
  if (ll) mp = write_ll_resync(mp, avail, ctx->utc_url, snap->part_target, 0);
  mp = WRITE_LIT(mp, "      <Representation id=\"video\" codecs=\"");
  mp = write_lit(mp, codecs, strlen(codecs));
  mp = WRITE_LIT(mp, "\" bandwidth=\"");
  mp = write_u64_gen(mp, mpd_bandwidth(vsnap), 0);
  mp = WRITE_LIT(mp, "\"");
  mp = write_video_attrs(mp, &vsnap->video);
  mp = WRITE_LIT(mp, ">\n");
  mp = split ? write_segment_template(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len - audio_max, s, vsnap, "dinitv.mp4", "dsegv$Time$.m4s", "", ll)
             : write_segment_template(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len, s, vsnap, "init.mp4", "dseg$Time$.m4s", "", ll);
  if (split) mp = write_audio_set(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len, ctx, avail, ll, 2);
  mp = WRITE_LIT(mp, "  </Period>\n");
  if (utc_len) mp = write_mpd_utc_timing(mp, ctx->utc_url);
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

/* one AdaptationSet per alternative, reuse base timeline */
static size_t build_mpd_lcevc_all(const mpd_fmt_ctx_t *ctx, char *mpd, size_t cap) {
  const hls_store_t *s = ctx->s;
  const hls_snapshot_t *snap = ctx->snap;
  const hls_snapshot_t *vsnap = ctx->vsnap ? ctx->vsnap : snap;
  int split = ctx->asnap != NULL;
  char *mp = mpd;
  char avail[32];
  char publish[32];
  char codecs[64];
  double min_update;
  double tsb_depth;
  double pres_delay;
  double min_buffer;
  uint64_t bandwidth;
  int alt_count = snap->lcevc_pid_count + 1;
  int ll = ctx->want_ll && snap->part_target > 0.0;
  size_t mpd_utc_len = xml_escaped_len(ctx->utc_url);
  size_t utc_len = ll ? mpd_utc_len : 0; /* per AdaptationSet */
  size_t tail_max;
  size_t audio_max;

  audio_max = split ? DASH_MPD_AUDIO_MAX + utc_len : 0;
  if (ll && DASH_MPD_HEAD_MAX + audio_max + 2 * utc_len + DASH_MPD_TAIL_MAX > cap) {
    ll = 0;
    utc_len = 0;
    audio_max = split ? DASH_MPD_AUDIO_MAX : 0;
  }
  if (DASH_MPD_HEAD_MAX + audio_max + mpd_utc_len + DASH_MPD_TAIL_MAX > cap) mpd_utc_len = 0;
  tail_max = DASH_MPD_TAIL_MAX + mpd_utc_len;

  mpd_avail_time(s, vsnap, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);
  bandwidth = mpd_bandwidth(vsnap);

  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, ll, snap->part_target, 0);
  mp = write_period_open(mp, vsnap);
  for (int alt = 0; alt < alt_count; alt++) {
    unsigned pid = alt == 0 ? 0 : snap->lcevc_pid[alt - 1];
    char query[24];
    sbuf_t qb;
    if ((size_t)(mp - mpd) + DASH_MPD_HEAD_MAX + utc_len + tail_max + audio_max > cap) break;
    sbuf_init(&qb, query, sizeof query);
    sbuf_add(&qb, "?lcevc=");
    if (pid) sbuf_add_u64(&qb, pid);
    else sbuf_add(&qb, "base");
    mp = WRITE_LIT(mp, "    <AdaptationSet id=\"");
    mp = write_u32(mp, (uint32_t)(alt + 1), 0);
    mp = WRITE_LIT(mp, "\" contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"");
    mp = write_start_with_sap(mp, &vsnap->video);
    mp = WRITE_LIT(mp, "\">\n"
      "      <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:adaptation-set-switching:2016\" value=\"");
    for (int k = 0, first = 1; k < alt_count; k++) {
      if (k == alt) continue;
      if (!first) mp = WRITE_LIT(mp, ",");
      mp = write_u32(mp, (uint32_t)(k + 1), 0);
      first = 0;
    }
    mp = WRITE_LIT(mp, "\"/>\n");
    if (ll) mp = write_ll_resync(mp, avail, ctx->utc_url, snap->part_target, 0);
    mp = WRITE_LIT(mp, "      <Representation id=\"");
    if (pid) mp = write_u32(mp, pid, 0); else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\" codecs=\"");
    mp = write_lit(mp, codecs, strlen(codecs));
    mp = WRITE_LIT(mp, "\" bandwidth=\"");
    mp = write_u64_gen(mp, bandwidth, 0);
    if (pid) mp = WRITE_LIT(mp, "\" dependencyId=\"base");
    mp = WRITE_LIT(mp, "\"");
    mp = write_video_attrs(mp, &vsnap->video);
    mp = WRITE_LIT(mp, ">\n");
    mp = split ? write_segment_template(mp, mpd, cap - tail_max - audio_max, s, vsnap, "dinitv.mp4", "dsegv$Time$.m4s", query, ll)
               : write_segment_template(mp, mpd, cap - tail_max, s, vsnap, "init.mp4", "dseg$Time$.m4s", query, ll);
  }
  if (split) mp = write_audio_set(mp, mpd, cap - tail_max, ctx, avail, ll, (unsigned)alt_count + 1);
  mp = WRITE_LIT(mp, "  </Period>\n");
  if (mpd_utc_len) mp = write_mpd_utc_timing(mp, ctx->utc_url);
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

static size_t fmt_mpd_cb(void *ctx_, char *buf, size_t cap) {
  return build_mpd(ctx_, buf, cap);
}

static size_t fmt_mpd_all_cb(void *ctx_, char *buf, size_t cap) {
  return build_mpd_lcevc_all(ctx_, buf, cap);
}

static const hls_snapshot_t *track_snap(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *t = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  return t ? atomic_load_explicit(&t->snap, memory_order_acquire) : NULL;
}

static int mpd_track_snaps(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, mpd_fmt_ctx_t *f) {
  const hls_snapshot_t *v;
  const hls_snapshot_t *a;
  f->vsnap = NULL;
  f->asnap = NULL;
  if (!f->snap->acodec_str[0]) return 1;
  v = track_snap(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4_VIDEO);
  a = track_snap(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4_AUDIO);
  if (!v || !a || v->count == 0 || a->count == 0 || !v->init_size || !a->init_size) return 0;
  if (v->epoch != f->snap->epoch || a->epoch != f->snap->epoch) return 0;
  f->vsnap = v;
  f->asnap = a;
  return 1;
}

static int dash_resolve_mpd(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url,
                            dash_mpd_resolve_t *r) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  hls_snapshot_t *snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  mpd_fmt_ctx_t fctx;
  const cached_text_t *ct;
  text_fmt_fn fmt;
  size_t cap;
  int owned;
  memset(r, 0, sizeof *r);
  if (!snap || snap->count == 0) {
    r->status = 404;
    return 1;
  }
  fctx.s = s;
  fctx.snap = snap;
  fctx.want_ll = want_ll;
  fctx.utc_url = utc_url;
  if (!mpd_track_snaps(ctx, filter, pmt_pid, lcevc, &fctx)) {
    r->status = 404;
    return 1;
  }
  if (lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
    fmt = fmt_mpd_all_cb;
    cap = DASH_MPD_ALL_BUF_CAP;
  } else {
    fmt = fmt_mpd_cb;
    cap = DASH_MPD_BUF_CAP;
  }
  ct = snapshot_cache_text_keyed(want_ll ? snap->cache_mpd_ll : snap->cache_mpd, MPD_VARIANTS, utc_url, fmt, &fctx, cap, &owned);
  if (owned) r->owned = ct;
  if (!ct) {
    r->status = 404;
    return 1;
  }
  r->status = 200;
  r->body = (uint8_t *)ct->text;
  r->body_len = ct->len;
  return 1;
}

int dash_serve(conn_t *c, const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  dash_mpd_resolve_t r;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  dash_resolve_mpd(ctx, filter, pmt_pid, lcevc, want_ll, utc_url, &r);
  if (r.status == 404) {
    respond_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  queue_text_resp(c, "application/dash+xml", (const char *)r.body, r.body_len, is_head, keep_alive, cors_hdr);
  if (r.owned) cached_text_release(r.owned);
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}

int dash_render(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url, int is_head, hls_resp_t *out) {
  dash_mpd_resolve_t r;
  memset(out, 0, sizeof *out);
  dash_resolve_mpd(ctx, filter, pmt_pid, lcevc, want_ll, utc_url, &r);
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  resp_set_zc_cached(out, 200, "application/dash+xml", NULL, r.body, r.body_len, is_head);
  if (r.owned) cached_text_release(r.owned);
  return 1;
}
