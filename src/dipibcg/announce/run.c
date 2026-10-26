/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "dipibcg/version.h"
#include "dipibcg/announce/announce_int.h"

int announce_log_quiet;

static void emit_metrics(metrics_exporter_t *mx, double now, const bcg_doc_t *doc, const bcg_out_t *o, const bcg_metrics_t *bm) {
  metrics_writer_t w;
  if (!metrics_exporter_due(mx, now)) return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION)) return;

  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_CONFIGURED, NULL, 1);
  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_UP, NULL, bm->sources_up ? 1 : 0);
  for (unsigned r = 0; r < NET_ERR_COUNT; r++) metrics_writer_put(&w, METRICS_ID_BCG_SOURCE_ERRORS_TOTAL, net_err_reason_name((net_err_reason_t)r), bm->source_errors_total[r]);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES, NULL, (uint64_t)doc->channel_count);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES_WITH_EVENTS, NULL, (uint64_t)o->nchan_prog);
  metrics_writer_put(&w, METRICS_ID_BCG_EVENTS, NULL, (uint64_t)o->nevents);
  metrics_writer_put(&w, METRICS_ID_BCG_DOCUMENTS_GENERATED_TOTAL, NULL, bm->documents_generated_total);
  metrics_writer_put(&w, METRICS_ID_BCG_DOCUMENT_ERRORS_TOTAL, NULL, bm->document_errors_total);
  metrics_writer_put(&w, METRICS_ID_BCG_PUBLICATIONS_TOTAL, NULL, bm->publications_total);
  metrics_writer_put(&w, METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL, NULL, bm->publication_errors_total);
  metrics_writer_put(&w, METRICS_ID_BCG_LAST_SUCCESS_TIME_SECONDS, NULL, (uint64_t)bm->last_success_time);
  metrics_writer_put(&w, METRICS_ID_BCG_SCHEDULE_START_TIME_SECONDS, NULL, o->have_sched ? (uint64_t)minutes_to_unix(o->sched_start) : 0);
  metrics_writer_put(&w, METRICS_ID_BCG_SCHEDULE_END_TIME_SECONDS, NULL, o->have_sched ? (uint64_t)minutes_to_unix(o->sched_end) : 0);
  metrics_exporter_send(mx, &w);
}

static void src_free(bcg_src_t *s) {
  bcg_doc_free(&s->doc);
  free(s->times);
  free(s->sel);
  free(s->sel_new);
  memset(s, 0, sizeof *s);
}

/* takes *nd on success. 0 ok, -1 oom (*nd untouched) */
static int src_set(bcg_src_t *s, const bcg_doc_t *nd) {
  prog_time_t *t;
  int *a = NULL;
  int *b = NULL;

  memset(s, 0, sizeof *s);
  if (prog_times_build(nd, &t)) return -1;
  if (nd->programme_count > 0) {
    a = malloc(sizeof *a * (size_t)nd->programme_count);
    b = malloc(sizeof *b * (size_t)nd->programme_count);
    if (!a || !b) {
      free(a);
      free(b);
      free(t);
      return -1;
    }
  }
  s->doc = *nd;
  s->times = t;
  s->sel = a;
  s->sel_new = b;
  return 0;
}

static void schema_free(bcg_schema_t *s) {
  if (!s) return;
  if (s->set) bcg_schema_close(s);
  free(s);
}

static void bcg_announce_ready(void *ctx_, const mcast_t *m) {
  const bcg_announce_ctx_t *ctx = ctx_;
  char mcast_txt[80];
  (void)m;
  mcast_describe(ctx->cfg, mcast_txt, sizeof mcast_txt);
  if (ctx->cfg->format != FMT_XMLTV) {
    log_line("announcing %d fragments of %s on %s every %lds", ctx->ntfrag, ctx->cfg->input_path, mcast_txt, ctx->cfg->interval_s);
    return;
  }
  log_line("announcing %d channels, %d programmes (window %ldh) on %s every %lds", ctx->src.doc.channel_count, ctx->src.doc.programme_count,
            ctx->cfg->window_hours, mcast_txt, ctx->cfg->interval_s);
}

static int group_xmltv_plain(bcg_announce_ctx_t *ctx, bcg_out_t *st, const bcg_doc_t *win, int g, const bcg_doc_index_t *ix, int from, int to, int *nfuu, int *ncp) {
  bcgau_frags_t fr;
  char *xml = NULL;
  size_t xl = 0;
  int sr;
  if (bcgau_frags_range(&ctx->sch->au, win, ix, from, to, ctx->au_flags, &fr, ncp)) return SEG_NO_DOC;
  *nfuu = (int)fr.n;
  sr = bcgau_xml(&ctx->sch->au, fr.f, fr.n, &xml, &xl) ? SEG_NO_DOC : build_plain(xml, xl, &st->vers[g], &st->seg[g]);
  free(xml);
  bcgau_frags_free(&fr);
  return sr;
}

/* container of channels from..to-1, SEG_* */
static int group_xmltv(bcg_announce_ctx_t *ctx, bcg_out_t *st, const bcg_doc_t *win, int g, const bcg_doc_index_t *ix, int from, int to, int *nfuu, int *ncp) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  size_t aul;
  size_t srl;
  const unsigned char *aud;
  const unsigned char *srd;
  int sr = SEG_NO_DOC;
  if (ctx->cfg->encoding == ENC_PLAIN) return group_xmltv_plain(ctx, st, win, g, ix, from, to, nfuu, ncp);
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) {
    bitwriter_free(&bw);
    return SEG_NO_DOC;
  }
  if (!bcgau_encode_range(&ctx->sc, &ctx->sch->au, win, ix, from, to, ctx->au_flags, &bw, &sw, nfuu, ncp)) {
    if (!segment_fits(&bw, &sw, (size_t)*nfuu)) {
      build_log("GE100145", "channel %s alone exceeds %u bytes per repository, reduce its programmes", win->channels[from].id, BIM_REPO_MAX);
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      return SEG_FAILED;
    }
    aud = bitwriter_data(&bw, &aul);
    srd = strrepo_writer_data(&sw, &srl);
    if (!frag_assign(ctx, win, aud, aul, srd, srl, ctx->sc.refs, *nfuu) && !index_collect(ctx, win, ix, ctx->sc.refs, *nfuu, (unsigned)g + 1))
      sr = build_segment(&bw, &sw, ctx->sc.refs, *nfuu, &st->vers[g], ctx->cfg->compress, &st->seg[g]);
  }
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return sr;
}

/* container of input fragments from..to-1, SEG_* */
static int group_tva(bcg_announce_ctx_t *ctx, bcg_out_t *st, int g, int from, int to, int *nfuu) {
  const dvbtva_fuu_t *f = &ctx->tfrag[from];
  size_t n = (size_t)(to - from);
  bitwriter_t bw;
  strrepo_writer_t sw;
  int sr = SEG_NO_DOC;
  *nfuu = (int)n;
  if (ctx->cfg->encoding == ENC_PLAIN) {
    bim_tvapart_t *cp;
    char *xml = NULL;
    size_t xl = 0;
    if (bim_tvafrag_copy(f, n, &cp)) return SEG_NO_DOC;
    sr = bcgau_xml(&ctx->sch->au, cp, n, &xml, &xl) ? SEG_NO_DOC : build_plain(xml, xl, &st->vers[g], &st->seg[g]);
    free(xml);
    bim_tvafrag_free(cp, n);
    return sr;
  }
  bitwriter_init(&bw);
  if (strrepo_writer_init(&sw)) {
    bitwriter_free(&bw);
    return SEG_NO_DOC;
  }
  if (!bcgau_write(&ctx->sc, &ctx->sch->au, f, n, NULL, &bw, &sw)) {
    size_t aul;
    size_t srl;
    const unsigned char *aud = bitwriter_data(&bw, &aul);
    const unsigned char *srd = strrepo_writer_data(&sw, &srl);
    if (!frag_assign_keys(ctx, key_node, f, aud, aul, srd, srl, ctx->sc.refs, (int)n)) sr = build_segment(&bw, &sw, ctx->sc.refs, (int)n, &st->vers[g], ctx->cfg->compress, &st->seg[g]);
  }
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return sr;
}

/* channel g's container of the previous build, its selection unchanged. refs as bcgau_frags_range orders them, SEG_* */
static int group_reuse(bcg_announce_ctx_t *ctx, bcg_out_t *st, const bcg_doc_t *win, int g, const bcg_doc_index_t *ix, int *nfuu, int *ncp) {
  const seg_cache_t *old = &ctx->out.seg[g];
  bcgau_ref_t *refs;
  int n = 0;
  *ncp = ix->nprog[g] > 0;
  if (win->channels[g].uri[0]) {
    if (array_reserve((void **)&ctx->sc.refs, &ctx->sc.refs_cap, ix->nprog[g] + 2, sizeof *ctx->sc.refs)) return SEG_NO_DOC;
    refs = ctx->sc.refs;
    for (int j = ix->first[g]; j >= 0; j = ix->next[j]) refs[n++] = (bcgau_ref_t){.kind = BCGAU_PROGRAM, .index = j};
    if (ix->nprog[g]) refs[n++] = (bcgau_ref_t){.kind = BCGAU_SCHEDULE, .index = g};
    refs[n++] = (bcgau_ref_t){.kind = BCGAU_SERVICE, .index = g};
    if (ctx->cfg->encoding != ENC_PLAIN && (frag_touch(ctx, win, refs, n) || index_collect(ctx, win, ix, refs, n, (unsigned)g + 1))) return SEG_NO_DOC;
  }
  *nfuu = n;
  if (!(st->seg[g].buf = malloc(old->len ? old->len : 1))) return SEG_NO_DOC;
  memcpy(st->seg[g].buf, old->buf, old->len);
  st->seg[g].len = old->len;
  st->nreused++;
  return SEG_OK;
}

/* gsel/goff of st from view: doc programme indices per channel container. 0 ok, -1 oom */
static int group_sels(bcg_announce_ctx_t *ctx, bcg_out_t *st, const bcg_doc_index_t *ix, int nchan) {
  int pos = 0;
  st->goff = malloc(sizeof *st->goff * ((size_t)nchan + 1));
  st->gsel = malloc(sizeof *st->gsel * ((size_t)ctx->src.nsel + 1));
  if (!st->goff || !st->gsel) return -1;
  for (int g = 0; g < nchan; g++) {
    st->goff[g] = pos;
    for (int j = ix->first[g]; j >= 0; j = ix->next[j]) st->gsel[pos++] = ctx->src.sel[j];
  }
  st->goff[nchan] = pos;
  return 0;
}

/* channel g selects the same programmes as in ctx->out */
static int group_same(const bcg_announce_ctx_t *ctx, const bcg_out_t *st, int g) {
  const bcg_out_t *o = &ctx->out;
  int n = st->goff[g + 1] - st->goff[g];
  if (!o->goff || o->src_gen != st->src_gen || g >= o->nseg || n != o->goff[g + 1] - o->goff[g]) return 0;
  return !n || !memcmp(st->gsel + st->goff[g], o->gsel + o->goff[g], sizeof *st->gsel * (size_t)n);
}

int announce_build(bcg_announce_ctx_t *ctx, long sched_start, long sched_end, int have_sched) {
  const int tva = ctx->cfg->format != FMT_XMLTV;
  int *bounds = NULL;
  int ngroups = 0;
  int per_channel;
  int rc = -1;
  bcg_doc_index_t ix = {0};
  bcg_doc_t win;
  bcg_out_t st;

  memset(&win, 0, sizeof win);
  if (out_stage(&st, &ctx->out)) return -1;
  st.src_gen = ctx->src_gen;
  if (!tva && (window_view(&ctx->src.doc, ctx->src.times, ctx->src.sel, ctx->src.nsel, &win) || bcg_doc_index_build(&ix, &win))) goto done;
  if ((tva ? plan_groups_frags(&ctx->sc, &ctx->sch->au, ctx->tfrag, ctx->ntfrag, &bounds, &ngroups) : plan_groups(&ctx->sc, &ctx->sch->au, &win, &ix, ctx->au_flags, &bounds, &ngroups)) ||
      grow_zeroed((void **)&st.vers, &st.vers_n, ngroups, sizeof *st.vers) || !(st.seg = calloc((size_t)ngroups + 1, sizeof *st.seg)))
    goto done;
  st.nseg = ngroups;
  per_channel = !tva && win.channel_count && ngroups == win.channel_count;
  if (per_channel && group_sels(ctx, &st, &ix, ngroups)) goto done;
  ctx->frag_gen++;
  index_reset(ctx);
  for (int g = 0; g < ngroups; g++) {
    int nfuu = 0;
    int ncp = 0;
    int sr;
    if (tva) sr = group_tva(ctx, &st, g, bounds[g], bounds[g + 1], &nfuu);
    else if (per_channel && group_same(ctx, &st, g)) sr = group_reuse(ctx, &st, &win, g, &ix, &nfuu, &ncp);
    else sr = group_xmltv(ctx, &st, &win, g, &ix, bounds[g], bounds[g + 1], &nfuu, &ncp);
    if (sr != SEG_OK) goto done;
    st.nfuu += nfuu;
    st.nchan_prog += ncp;
  }
  if (build_tva_main(ctx, &st)) goto done;
  frag_prune(ctx);
  if (index_build(ctx, &st, (unsigned)ngroups + 1)) goto done;
  if (ctx->cfg->encoding != ENC_PLAIN) version_stage(&st.init_ver, ctx->sch->init, ctx->sch->init_len);
  if (!tva) {
    /* announced: channels with uri */
    st.nchan_prog = 0;
    for (int c = 0; c < win.channel_count; c++) {
      if (!win.channels[c].uri[0]) continue;
      st.nevents += ix.nprog[c];
      st.nchan_prog += ix.nprog[c] > 0;
    }
  }
  st.sched_start = sched_start;
  st.sched_end = sched_end;
  st.have_sched = have_sched;
  out_commit(&ctx->out, &st);
  rc = 0;

done:
  if (rc) out_discard(&st, &ctx->out);
  window_view_free(&win);
  free(bounds);
  bcg_doc_index_free(&ix);
  return rc;
}

/* window of now, then build. 0 ok, -1 error (logged) */
static int build_now(bcg_announce_ctx_t *ctx, int startup) {
  long ss = 0;
  long se = 0;
  int have = 0;
  if (ctx->cfg->format == FMT_XMLTV)
    window_select(&ctx->src.doc, ctx->src.times, now_minutes(), (int64_t)ctx->cfg->window_hours * 60, ctx->src.sel, &ctx->src.nsel, &ss, &se, &have);
  announce_log_quiet = 0;
  ctx->cache_valid = !announce_build(ctx, ss, se, have);
  ctx->build_failing = !ctx->cache_valid;
  if (!ctx->cache_valid) log_code(startup ? "GS100153" : "GE100153", "guide build failed");
  return ctx->cache_valid ? 0 : -1;
}

static int reload_tva(bcg_announce_ctx_t *ctx) {
  bcg_schema_t *old_sch = ctx->sch;
  dvbtva_fuu_t *old_f = ctx->tfrag;
  int old_n = ctx->ntfrag;
  int old_valid = ctx->cache_valid;
  if (tvadoc_load(ctx->cfg, 0, old_sch->ns, &ctx->sch, &ctx->tfrag, &ctx->ntfrag)) return -1;
  if (build_now(ctx, 0)) {
    dvbtva_au_free(ctx->tfrag, (size_t)ctx->ntfrag);
    schema_free(ctx->sch);
    ctx->sch = old_sch;
    ctx->tfrag = old_f;
    ctx->ntfrag = old_n;
    ctx->cache_valid = old_valid;
    return -1;
  }
  dvbtva_au_free(old_f, (size_t)old_n);
  schema_free(old_sch);
  log_line("reloaded %d fragments from %s", ctx->ntfrag, ctx->cfg->input_path);
  return 0;
}

static int reload_xmltv(bcg_announce_ctx_t *ctx) {
  bcg_src_t old = ctx->src;
  int old_valid = ctx->cache_valid;
  bcg_doc_t reloaded;
  if (load_doc(ctx->cfg, 0, &reloaded)) return -1;
  if (src_set(&ctx->src, &reloaded)) {
    bcg_doc_free(&reloaded);
    ctx->src = old;
    return -1;
  }
  ctx->src_gen++;
  if (build_now(ctx, 0)) {
    src_free(&ctx->src);
    ctx->src = old;
    ctx->src_gen--;
    ctx->cache_valid = old_valid;
    return -1;
  }
  src_free(&old);
  log_line("reloaded %d channels, %d programmes from %s / %s", ctx->src.doc.channel_count, ctx->src.doc.programme_count, ctx->cfg->input_path, ctx->cfg->map_path);
  return 0;
}

static void bcg_announce_reload(void *ctx_) {
  bcg_announce_ctx_t *ctx = ctx_;
  if (!(ctx->cfg->format == FMT_XMLTV ? reload_xmltv(ctx) : reload_tva(ctx))) {
    if (ctx->metrics_on) ctx->bm.sources_up = 1;
    return;
  }
  log_code("GW000101", "reload failed, keeping previous guide");
  if (!ctx->metrics_on) return;
  ctx->bm.sources_up = 0;
  ctx->bm.source_errors_total[NET_ERR_FORMAT]++;
}

static int bcg_announce_cycle(void *ctx_, mcast_t *m, unsigned cycle) {
  bcg_announce_ctx_t *ctx = ctx_;
  const bcg_out_t *o = &ctx->out;
  int nnew = 0;
  int failed = 0;
  int refresh_due = ctx->cfg->refresh_cycles < 2 || (cycle - 1) % (unsigned)ctx->cfg->refresh_cycles == 0;
  long ss = 0;
  long se = 0;
  int have = 0;

  const int tva = ctx->cfg->format != FMT_XMLTV;
  const int plain = ctx->cfg->encoding == ENC_PLAIN;

  if (!tva) window_select(&ctx->src.doc, ctx->src.times, now_minutes(), (int64_t)ctx->cfg->window_hours * 60, ctx->src.sel_new, &nnew, &ss, &se, &have);
  if (!ctx->cache_valid || (!tva && (nnew != ctx->src.nsel || (nnew > 0 && memcmp(ctx->src.sel, ctx->src.sel_new, sizeof *ctx->src.sel * (size_t)nnew))))) {
    if (!tva) {
      int *tmp = ctx->src.sel;
      ctx->src.sel = ctx->src.sel_new;
      ctx->src.sel_new = tmp;
      ctx->src.nsel = nnew;
    }
    announce_log_quiet = ctx->build_failing;
    ctx->cache_valid = !announce_build(ctx, ss, se, have);
    announce_log_quiet = 0;
    if (!ctx->cache_valid) {
      if (!ctx->build_failing) log_code("GE100153", "guide build failed");
      if (ctx->metrics_on) ctx->bm.document_errors_total++;
    }
    ctx->build_failing = !ctx->cache_valid;
  }
  if (!plain && send_tva_init(m, ctx->sch, o->init_ver.version)) failed = 1;
  if (ctx->cfg->publisher && send_tva_main(m, !plain, &o->main_seg, o->main_ver.version)) failed = 1;
  for (int g = 0; g < o->nseg; g++) {
    bcg_version_t *v = &o->vers[g];
    if (!v->unsent && !refresh_due) continue;
    if (send_segment(m, !plain, &o->seg[g], v->version, (unsigned)g + 1, (int64_t)ctx->cfg->interval_s * 500000000 / o->nseg)) failed = 1;
    else v->unsent = 0;
  }
  for (int i = 0; i < o->niseg; i++) {
    bcg_version_t *v = &o->ivers[i];
    if (!v->unsent && !refresh_due) continue;
    if (send_index(m, &o->iseg[i], v->version, o->iseg_id[i], (int64_t)ctx->cfg->interval_s * 500000000 / o->niseg)) failed = 1;
    else v->unsent = 0;
  }
  if (ctx->metrics_on) {
    ctx->bm.documents_generated_total++;
    if (failed) {
      ctx->bm.publication_errors_total++;
    } else {
      ctx->bm.publications_total++;
      ctx->bm.last_success_time = (double)time(NULL);
    }
  }
  if (ctx->cfg->verbose) log_code("GD100100", "cycle %u sent, %d fragments in %d containers", cycle, o->nfuu, o->nseg);
  emit_metrics(ctx->mx, mono_seconds(), &ctx->src.doc, o, &ctx->bm);
  return 0;
}

static void bcg_announce_cleanup(void *ctx_) {
  bcg_announce_ctx_t *ctx = ctx_;
  bcgau_scratch_free(&ctx->sc);
  dvbtva_au_free(ctx->tfrag, (size_t)ctx->ntfrag);
  schema_free(ctx->sch);
  index_free(ctx);
  frag_free(ctx);
  out_free(&ctx->out);
  src_free(&ctx->src);
}

static int start_xmltv(bcg_announce_ctx_t *ctx) {
  bcg_doc_t first;
  if (load_doc(ctx->cfg, 1, &first)) return -1;
  if (!(ctx->sch = calloc(1, sizeof *ctx->sch))) log_code("GS500101", "out of memory");
  if (!ctx->sch || bcg_schema_open(ctx->sch, ctx->cfg, bcg_doc_ns(&first), 1) || src_set(&ctx->src, &first)) {
    bcg_doc_free(&first);
    return -1;
  }
  return 0;
}

int announce_run(const config_t *cfg, metrics_exporter_t *mx) {
  bcg_announce_ctx_t ctx;
  announce_driver_t d;

  memset(&ctx, 0, sizeof ctx);
  ctx.cfg = cfg;
  ctx.mx = mx;
  ctx.metrics_on = metrics_exporter_enabled(mx);
  ctx.au_flags = cfg->index_mask & 1u << BIM_INDEX_SCHED_TITLE ? BCGAU_SCHED_TITLES : 0;
  bcgau_scratch_init(&ctx.sc);
  if (cfg->format == FMT_XMLTV ? start_xmltv(&ctx) : tvadoc_load(cfg, 1, NULL, &ctx.sch, &ctx.tfrag, &ctx.ntfrag)) {
    if (ctx.metrics_on) ctx.bm.source_errors_total[NET_ERR_FORMAT]++;
    bcg_announce_cleanup(&ctx);
    return 1;
  }
  if (build_now(&ctx, 1)) {
    bcg_announce_cleanup(&ctx);
    return 1;
  }
  if (ctx.metrics_on) ctx.bm.sources_up = 1;

  d = (announce_driver_t){
    .ctx = &ctx,
    .on_ready = bcg_announce_ready,
    .reload = bcg_announce_reload,
    .run_cycle = bcg_announce_cycle,
    .cleanup = bcg_announce_cleanup,
    .family = cfg->family,
    .mcast_group = cfg->mcast_group,
    .mcast_port = cfg->mcast_port,
    .iface = cfg->iface,
    .dscp = cfg->dscp,
    .ttl = (int)cfg->ttl,
    .interval_s = cfg->interval_s,
  };
  return announce_driver_run(&d);
}
