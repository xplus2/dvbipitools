/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipibcg/announce.h"
#include "dipibcg/container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/announce_driver.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "dipibcg/version.h"
#include "dipibcg/wrapper.h"
#include "dipibcg/announce/announce_int.h"

static void emit_metrics(metrics_exporter_t *mx, double now, const bcg_doc_t *doc, const bcg_doc_t *windowed, int services_with_events, const bcg_metrics_t *bm,
                          long sched_start, long sched_end, int have_sched) {
  metrics_writer_t w;
  if (!metrics_exporter_due(mx, now)) return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION)) return;

  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_CONFIGURED, NULL, 1);
  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_UP, NULL, bm->sources_up ? 1 : 0);
  for (unsigned r = 0; r < NET_ERR_COUNT; r++) metrics_writer_put(&w, METRICS_ID_BCG_SOURCE_ERRORS_TOTAL, net_err_reason_name((net_err_reason_t)r), bm->source_errors_total[r]);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES, NULL, (uint64_t)doc->channel_count);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES_WITH_EVENTS, NULL, (uint64_t)services_with_events);
  metrics_writer_put(&w, METRICS_ID_BCG_EVENTS, NULL, (uint64_t)windowed->programme_count);
  metrics_writer_put(&w, METRICS_ID_BCG_DOCUMENTS_GENERATED_TOTAL, NULL, bm->documents_generated_total);
  metrics_writer_put(&w, METRICS_ID_BCG_DOCUMENT_ERRORS_TOTAL, NULL, bm->document_errors_total);
  metrics_writer_put(&w, METRICS_ID_BCG_PUBLICATIONS_TOTAL, NULL, bm->publications_total);
  metrics_writer_put(&w, METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL, NULL, bm->publication_errors_total);
  metrics_writer_put(&w, METRICS_ID_BCG_LAST_SUCCESS_TIME_SECONDS, NULL, (uint64_t)bm->last_success_time);
  metrics_writer_put(&w, METRICS_ID_BCG_SCHEDULE_START_TIME_SECONDS, NULL, have_sched ? (uint64_t)minutes_to_unix(sched_start) : 0);
  metrics_writer_put(&w, METRICS_ID_BCG_SCHEDULE_END_TIME_SECONDS, NULL, have_sched ? (uint64_t)minutes_to_unix(sched_end) : 0);
  metrics_exporter_send(mx, &w);
}

static void ctx_drop_cache(bcg_announce_ctx_t *ctx) {
  for (int i = 0; i < ctx->nseg; i++) free(ctx->seg[i].buf);
  if (ctx->seg) memset(ctx->seg, 0, sizeof *ctx->seg * (size_t)ctx->nseg);
  window_view_free(&ctx->win);
  ctx->cache_valid = 0;
}

/* takes *nd on success. 0 ok, -1 oom (*nd untouched) */
static int ctx_set_doc(bcg_announce_ctx_t *ctx, const bcg_doc_t *nd) {
  prog_time_t *t;
  int *a = NULL;
  int *b = NULL;

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
  ctx_drop_cache(ctx);
  bcg_doc_free(&ctx->doc);
  ctx->doc = *nd;
  free(ctx->times);
  free(ctx->sel);
  free(ctx->sel_new);
  ctx->times = t;
  ctx->sel = a;
  ctx->sel_new = b;
  ctx->nsel = 0;
  return 0;
}

static int grow_versions(bcg_announce_ctx_t *ctx, int n) {
  bcg_version_t *np = realloc(ctx->vers, sizeof *np * (size_t)n);
  if (!np) return -1;
  memset(np + ctx->vers_n, 0, sizeof *np * (size_t)(n - ctx->vers_n));
  ctx->vers = np;
  ctx->vers_n = n;
  return 0;
}

static int grow_segs(bcg_announce_ctx_t *ctx, int n) {
  seg_cache_t *np;
  if (n <= ctx->nseg) return 0;
  np = realloc(ctx->seg, sizeof *np * (size_t)n);
  if (!np) return -1;
  memset(np + ctx->nseg, 0, sizeof *np * (size_t)(n - ctx->nseg));
  ctx->seg = np;
  ctx->nseg = n;
  return 0;
}

static void bcg_announce_ready(void *ctx_, const mcast_t *m) {
  const bcg_announce_ctx_t *ctx = ctx_;
  char mcast_txt[80];
  (void)m;
  mcast_describe(ctx->cfg, mcast_txt, sizeof mcast_txt);
  log_line("announcing %d channels, %d programmes (window %ldh) on %s every %lds", ctx->doc.channel_count, ctx->doc.programme_count,
            ctx->cfg->window_hours, mcast_txt, ctx->cfg->interval_s);
}

static void bcg_announce_reload(void *ctx_) {
  bcg_announce_ctx_t *ctx = ctx_;
  bcg_doc_t reloaded;
  if (load_doc(ctx->cfg, &reloaded)) goto failed;
  if (ctx_set_doc(ctx, &reloaded)) {
    bcg_doc_free(&reloaded);
    goto failed;
  }
  log_line("reloaded %d channels, %d programmes from %s / %s", ctx->doc.channel_count, ctx->doc.programme_count, ctx->cfg->input_path, ctx->cfg->map_path);
  if (ctx->metrics_on) ctx->bm.sources_up = 1;
  return;
failed:
  log_code("GW000101", "reload failed, keeping previous guide");
  if (!ctx->metrics_on) return;
  ctx->bm.sources_up = 0;
  ctx->bm.source_errors_total[NET_ERR_FORMAT]++;
}

/* encodes and wraps all containers into ctx->seg. 0 ok, -1 doc error */
static int cycle_build(bcg_announce_ctx_t *ctx) {
  int *bounds = NULL;
  int ngroups = 0;
  int ok = 1;
  int rc = -1;
  accessunit_index_t ix;

  ctx_drop_cache(ctx);
  if (window_view(&ctx->doc, ctx->sel, ctx->nsel, &ctx->win)) return -1;
  if (accessunit_index_build(&ix, &ctx->win)) return -1;
  if (plan_groups(&ctx->sc, &ctx->win, &ix, &bounds, &ngroups) || (ngroups > ctx->vers_n && grow_versions(ctx, ngroups)) || grow_segs(ctx, ngroups)) goto done;
  ctx->nfuu_total = 0;
  ctx->nchan_prog = 0;
  for (int g = 0; g < ngroups; g++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    int nfuu = 0;
    int ncp = 0;
    int sr;
    bitwriter_init(&bw);
    if (strrepo_writer_init(&sw)) {
      bitwriter_free(&bw);
      ctx_drop_cache(ctx);
      goto done;
    }
    if (accessunit_encode_range(&ctx->sc, &ctx->win, &ix, bounds[g], bounds[g + 1], &bw, &sw, &nfuu, &ncp)) {
      sr = SEG_NO_DOC;
    } else {
      ctx->nfuu_total += nfuu;
      ctx->nchan_prog += ncp;
      sr = build_segment(&bw, &sw, ctx->sc.refs, nfuu, &ctx->vers[g], ctx->cfg->compress, &ctx->seg[g]);
    }
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    if (sr == SEG_NO_DOC) {
      ctx_drop_cache(ctx);
      goto done;
    }
    if (sr != SEG_OK) ok = 0;
  }
  ctx->cache_valid = ok;
  ctx->ngroups = ngroups;
  rc = 0;

done:
  free(bounds);
  accessunit_index_free(&ix);
  return rc;
}

static int bcg_announce_cycle(void *ctx_, mcast_t *m, unsigned cycle) {
  bcg_announce_ctx_t *ctx = ctx_;
  int nnew;
  int failed = 0;
  int refresh_due = ctx->cfg->refresh_cycles < 2 || (cycle - 1) % (unsigned)ctx->cfg->refresh_cycles == 0;
  long ss;
  long se;
  int have;
  int *tmp;

  window_select(&ctx->doc, ctx->times, now_minutes(), (int64_t)ctx->cfg->window_hours * 60, ctx->sel_new, &nnew, &ss, &se, &have);
  if (!ctx->cache_valid || nnew != ctx->nsel || (nnew > 0 && memcmp(ctx->sel, ctx->sel_new, sizeof *ctx->sel * (size_t)nnew))) {
    tmp = ctx->sel;
    ctx->sel = ctx->sel_new;
    ctx->sel_new = tmp;
    ctx->nsel = nnew;
    ctx->sched_start = ss;
    ctx->sched_end = se;
    ctx->have_sched = have;
    if (cycle_build(ctx)) {
      if (ctx->metrics_on) ctx->bm.document_errors_total++;
      return -1;
    }
  }
  for (int g = 0; g < ctx->ngroups; g++) {
    bcg_version_t *v = &ctx->vers[g];
    if (!ctx->seg[g].buf) {
      failed = 1;
    } else if (v->unsent || refresh_due) {
      if (send_segment(m, &ctx->seg[g], v->version, (unsigned)g + 1, (int64_t)ctx->cfg->interval_s * 500000000 / ctx->ngroups)) failed = 1;
      else v->unsent = 0;
    }
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
  if (ctx->cfg->verbose) log_code("GD100100", "cycle %u sent, %d fragments in %d containers", cycle, ctx->nfuu_total, ctx->ngroups);
  emit_metrics(ctx->mx, mono_seconds(), &ctx->doc, &ctx->win, ctx->nchan_prog, &ctx->bm, ctx->sched_start, ctx->sched_end, ctx->have_sched);
  return 0;
}

static void bcg_announce_cleanup(void *ctx_) {
  bcg_announce_ctx_t *ctx = ctx_;
  accessunit_scratch_free(&ctx->sc);
  ctx_drop_cache(ctx);
  free(ctx->seg);
  for (int i = 0; i < ctx->vers_n; i++) free(ctx->vers[i].last);
  free(ctx->vers);
  free(ctx->times);
  free(ctx->sel);
  free(ctx->sel_new);
  bcg_doc_free(&ctx->doc);
}

int announce_run(const config_t *cfg, metrics_exporter_t *mx) {
  bcg_announce_ctx_t ctx;
  announce_driver_t d;
  bcg_doc_t first;

  memset(&ctx, 0, sizeof ctx);
  ctx.cfg = cfg;
  ctx.mx = mx;
  ctx.metrics_on = metrics_exporter_enabled(mx);
  accessunit_scratch_init(&ctx.sc);
  if (load_doc(cfg, &first)) {
    if (ctx.metrics_on) ctx.bm.source_errors_total[NET_ERR_FORMAT]++;
    accessunit_scratch_free(&ctx.sc);
    return 1;
  }
  if (ctx_set_doc(&ctx, &first)) {
    bcg_doc_free(&first);
    accessunit_scratch_free(&ctx.sc);
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
    .interval_s = cfg->interval_s,
  };
  return announce_driver_run(&d);
}
