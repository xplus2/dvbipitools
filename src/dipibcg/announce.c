/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "announce.h"
#include "container.h"
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
#include "version.h"
#include "wrapper.h"

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since MJD epoch, UTC-normalized */
int iso8601_to_minutes(const char *in, long *out) {
  iso8601_t f;
  if (iso8601_split(in, &f)) return -1;
  *out = date_to_mjd(f.y, f.mo, f.d) * 1440L + f.h * 60L + f.mi - f.off_min;
  return 0;
}

static long now_minutes(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  return date_to_mjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 1440L + tm.tm_hour * 60L + tm.tm_min;
}

/* MJD epoch (date_to_mjd 1970-01-01) is day 40587 */
long minutes_to_unix(long mjd_minutes) {
  return (mjd_minutes - 40587L * 1440L) * 60L;
}

typedef struct {
  int sources_up;
  unsigned long long source_errors_total[NET_ERR_COUNT];
  unsigned long long documents_generated_total;
  unsigned long long document_errors_total;
  unsigned long long publications_total;
  unsigned long long publication_errors_total;
  double last_success_time; /* unix seconds, 0 = never */
} bcg_metrics_t;

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

typedef struct {
  const bcg_programme_t *pr;
  long start;
  long end;
  int has_end;
  int idx;
} prog_ord_t;

static int prog_ord_cmp(const void *a, const void *b) {
  const prog_ord_t *x = a, *y = b;
  int c = strcmp(x->pr->channel_id, y->pr->channel_id);
  if (c) return c;
  return (x->start > y->start) - (x->start < y->start);
}

static void resolve_open_ends(prog_ord_t *ord, int n) {
  for (int i = 0; i < n; i++) {
    int j = i + 1;
    if (ord[i].has_end) continue;
    ord[i].end = ord[i].start;
    while (j < n && !strcmp(ord[j].pr->channel_id, ord[i].pr->channel_id) && ord[j].start == ord[i].start) j++;
    if (j < n && !strcmp(ord[j].pr->channel_id, ord[i].pr->channel_id)) {
      ord[i].end = ord[j].start;
      ord[i].has_end = 1;
    }
  }
}

int prog_times_build(const bcg_doc_t *doc, prog_time_t **out) {
  int n = 0;
  prog_ord_t *ord = doc->programme_count > 0 ? malloc(sizeof *ord * (size_t)doc->programme_count) : NULL;
  prog_time_t *t = doc->programme_count > 0 ? calloc((size_t)doc->programme_count, sizeof *t) : NULL;

  *out = NULL;
  if (doc->programme_count > 0 && (!ord || !t)) {
    free(ord);
    free(t);
    return -1;
  }
  for (int i = 0; i < doc->programme_count; i++) {
    const bcg_programme_t *pr = &doc->programmes[i];
    long e;
    if (iso8601_to_minutes(pr->start, &ord[n].start)) continue;
    ord[n].pr = pr;
    ord[n].idx = i;
    ord[n].has_end = pr->stop[0] && !iso8601_to_minutes(pr->stop, &e);
    ord[n].end = ord[n].has_end ? e : 0;
    n++;
  }
  qsort(ord, (size_t)n, sizeof *ord, prog_ord_cmp);
  resolve_open_ends(ord, n);
  for (int i = 0; i < n; i++) {
    prog_time_t *p = &t[ord[i].idx];
    p->start = ord[i].start;
    p->end = ord[i].end;
    p->has_end = ord[i].has_end;
    p->valid = 1;
  }
  free(ord);
  *out = t;
  return 0;
}

void window_select(const bcg_doc_t *src, const prog_time_t *times, long now, int64_t window_min, int *sel, int *nsel, long *out_sched_start, long *out_sched_end, int *out_have_sched) {
  long sched_start = 0, sched_end = 0;
  int have_sched = 0, n = 0;

  for (int i = 0; i < src->programme_count; i++) {
    const prog_time_t *t = &times[i];
    if (!t->valid) continue;
    if (t->has_end && t->end < now) continue;
    if ((int64_t)t->start > (int64_t)now + window_min) continue;
    sel[n++] = i;
    if (!have_sched || t->start < sched_start) sched_start = t->start;
    if (!have_sched || t->end > sched_end) sched_end = t->end;
    have_sched = 1;
  }
  *nsel = n;
  if (out_sched_start) *out_sched_start = sched_start;
  if (out_sched_end) *out_sched_end = sched_end;
  if (out_have_sched) *out_have_sched = have_sched;
}

int window_view(bcg_doc_t *src, const int *sel, int nsel, bcg_doc_t *dst) {
  memset(dst, 0, sizeof *dst);
  dst->channels = src->channels;
  dst->channel_count = src->channel_count;
  if (nsel <= 0) return 0;
  dst->programmes = malloc(sizeof *dst->programmes * (size_t)nsel);
  if (!dst->programmes) return -1;
  for (int i = 0; i < nsel; i++) dst->programmes[i] = src->programmes[sel[i]];
  dst->programme_count = nsel;
  dst->programme_cap = nsel;
  return 0;
}

void window_view_free(bcg_doc_t *view) {
  free(view->programmes);
  memset(view, 0, sizeof *view);
}

int load_doc(const config_t *cfg, bcg_doc_t *out) {
  mapping_t map;
  FILE *in;

  in = strcmp(cfg->input_path, "-") ? fopen(cfg->input_path, "r") : stdin;
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
  if (mapping_load(cfg->map_path, &map)) {
    bcg_doc_free(out);
    return -1;
  }
  for (int i = 0; i < out->channel_count; i++) {
    bcg_channel_t *c = &out->channels[i];
    char uri[BCG_ID_LEN];
    unsigned tsid, onid, sid;
    if (!mapping_lookup(&map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
      bufcpy(c->uri, sizeof c->uri, uri);
      c->tsid = tsid;
      c->onid = onid;
      c->sid = sid;
    }
  }
  mapping_free(&map);
  return 0;
}

void version_track(bcg_version_t *v, const unsigned char *cont, size_t cont_len) {
  unsigned char *copy;
  if (v->last && v->last_len == cont_len && !memcmp(v->last, cont, cont_len)) return;
  copy = malloc(cont_len ? cont_len : 1);
  if (!copy) {
    log_code("GE500233", "out of memory, version not advanced");
    return;
  }
  v->version = v->version % 255 + 1;
  v->unsent = 1;
  memcpy(copy, cont, cont_len);
  free(v->last);
  v->last = copy;
  v->last_len = cont_len;
}

enum { SEG_OK, SEG_FAILED, SEG_NO_DOC };

#define BCG_MAX_SEGMENTS 0xFFFFu

typedef struct {
  unsigned char *buf; /* wrapped segment payload */
  size_t len;
} seg_cache_t;

static int build_segment(bitwriter_t *bw, const strrepo_writer_t *sw, const accessunit_ref_t *refs, int nrefs, bcg_version_t *ver, int compress, seg_cache_t *seg) {
  size_t bits_len, strs_len, cont_len;
  const unsigned char *bits = bitwriter_data(bw, &bits_len);
  const unsigned char *strs = strrepo_writer_data(sw, &strs_len);
  unsigned char *cont;
  unsigned char *wrapped;
  size_t wrapped_len;
  int rc = SEG_FAILED;

  if (container_build(bits, bits_len, strs, strs_len, refs, nrefs, &cont, &cont_len) != 0) {
    log_code("GE100141", "container build failed");
    return SEG_NO_DOC;
  }
  version_track(ver, cont, cont_len);
  if (wrapper_build(cont, cont_len, compress, &wrapped, &wrapped_len) != 0) {
    log_code("GE100142", "wrapper build failed");
  } else if (wrapped_len > DVBSTP_MAX_SEGMENT) {
    log_code("GE100143", "guide too large (%zu bytes, max %u), reduce window or channels", wrapped_len, (unsigned)DVBSTP_MAX_SEGMENT);
    free(wrapped);
  } else {
    seg->buf = wrapped;
    seg->len = wrapped_len;
    rc = SEG_OK;
  }
  free(cont);
  return rc;
}

static int send_segment(mcast_t *m, const seg_cache_t *seg, unsigned version, unsigned segment_id, int64_t budget_ns) {
  if (dvbstp_send_segment(m, &(dvbstp_send_t){.payload_id = DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, .segment_id = segment_id, .segment_version = version, .compr = 1, .want_crc = 1, .pacing_budget_ns = budget_ns}, seg->buf, seg->len) == 0)
    return 0;
  log_code("GE100144", "send failed");
  return -1;
}

static size_t vlu8_len(size_t n) {
  size_t len = 1;
  while (n >= 128) {
    n >>= 7;
    len++;
  }
  return len;
}

static int plan_groups(accessunit_scratch_t *sc, const bcg_doc_t *doc, int **bounds, int *ngroups) {
  int n = doc->channel_count;
  int *b = malloc(sizeof *b * ((size_t)n + 2));
  size_t grp_au_body = 0, grp_str_body = 0, grp_nfuu = 0;
  int ng = 0;
  int per_channel = n <= (int)BCG_MAX_SEGMENTS;
  bitwriter_t bw;
  strrepo_writer_t sw;
  int rc = -1;

  if (!b) return -1;
  b[0] = 0;
  bitwriter_init(&bw);
  strrepo_writer_init(&sw);
  for (int c = 0; c < n; c++) {
    size_t au_len, str_len, au_body, str_body;
    int nf = 0;
    bitwriter_reset(&bw);
    strrepo_writer_free(&sw);
    strrepo_writer_init(&sw);
    if (accessunit_encode_range(sc, doc, c, c + 1, &bw, &sw, &nf, NULL)) goto done;
    bitwriter_data(&bw, &au_len);
    strrepo_writer_data(&sw, &str_len);
    au_body = au_len - vlu8_len((size_t)nf);
    str_body = str_len - 1;
    if (vlu8_len((size_t)nf) + au_body > CONTAINER_MAX_REPO || 1 + str_body > CONTAINER_MAX_REPO) {
      log_code("GE100145", "channel %s alone exceeds %u bytes per repository, reduce its programmes", doc->channels[c].id, (unsigned)CONTAINER_MAX_REPO);
      goto done;
    }
    if (c > 0 && (per_channel || vlu8_len(grp_nfuu + (size_t)nf) + grp_au_body + au_body > CONTAINER_MAX_REPO || 1 + grp_str_body + str_body > CONTAINER_MAX_REPO)) {
      if ((unsigned)ng + 1 >= BCG_MAX_SEGMENTS) {
        log_code("GE100147", "guide needs more than %u segments, reduce channels", (unsigned)BCG_MAX_SEGMENTS);
        goto done;
      }
      b[++ng] = c;
      grp_au_body = grp_str_body = grp_nfuu = 0;
    }
    grp_au_body += au_body;
    grp_str_body += str_body;
    grp_nfuu += (size_t)nf;
  }
  b[++ng] = n;
  *bounds = b;
  *ngroups = ng;
  b = NULL;
  rc = 0;

done:
  free(b);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  return rc;
}

typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  bcg_doc_t doc;
  prog_time_t *times; /* per doc programme, parsed at load */
  int *sel;           /* programme indices of last build */
  int *sel_new;
  int nsel;
  bcg_doc_t win;      /* view on doc for sel */
  long sched_start;
  long sched_end;
  int have_sched;
  seg_cache_t *seg;   /* per container, wrapped */
  int nseg;
  int cache_valid;
  int ngroups;
  int nfuu_total;
  int nchan_prog; /* windowed channels with events */
  bcg_metrics_t bm;
  accessunit_scratch_t sc;
  bcg_version_t *vers; /* per segment_id - 1 */
  int vers_n;
  int metrics_on;
} bcg_announce_ctx_t;

static void ctx_drop_cache(bcg_announce_ctx_t *ctx) {
  for (int i = 0; i < ctx->nseg; i++) free(ctx->seg[i].buf);
  if (ctx->seg) memset(ctx->seg, 0, sizeof *ctx->seg * (size_t)ctx->nseg);
  window_view_free(&ctx->win);
  ctx->cache_valid = 0;
}

/* takes *nd on success. 0 ok, -1 oom (*nd untouched) */
static int ctx_set_doc(bcg_announce_ctx_t *ctx, bcg_doc_t *nd) {
  prog_time_t *t;
  int *a = NULL, *b = NULL;

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

  ctx_drop_cache(ctx);
  if (window_view(&ctx->doc, ctx->sel, ctx->nsel, &ctx->win)) return -1;
  if (plan_groups(&ctx->sc, &ctx->win, &bounds, &ngroups) || (ngroups > ctx->vers_n && grow_versions(ctx, ngroups)) || grow_segs(ctx, ngroups)) {
    free(bounds);
    return -1;
  }
  ctx->nfuu_total = 0;
  ctx->nchan_prog = 0;
  for (int g = 0; g < ngroups; g++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    int nfuu = 0, ncp = 0;
    int rc;
    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    if (accessunit_encode_range(&ctx->sc, &ctx->win, bounds[g], bounds[g + 1], &bw, &sw, &nfuu, &ncp)) {
      rc = SEG_NO_DOC;
    } else {
      ctx->nfuu_total += nfuu;
      ctx->nchan_prog += ncp;
      rc = build_segment(&bw, &sw, ctx->sc.refs, nfuu, &ctx->vers[g], ctx->cfg->compress, &ctx->seg[g]);
    }
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    if (rc == SEG_NO_DOC) {
      free(bounds);
      ctx_drop_cache(ctx);
      return -1;
    }
    if (rc != SEG_OK) ok = 0;
  }
  free(bounds);
  ctx->cache_valid = ok;
  ctx->ngroups = ngroups;
  return 0;
}

static int bcg_announce_cycle(void *ctx_, mcast_t *m, unsigned cycle) {
  bcg_announce_ctx_t *ctx = ctx_;
  int nnew, failed = 0;
  int refresh_due = ctx->cfg->refresh_cycles < 2 || (cycle - 1) % (unsigned)ctx->cfg->refresh_cycles == 0;
  long ss, se;
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
