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
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/export.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/helper/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "version.h"
#include "wrapper.h"

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since MJD epoch, UTC-normalized */
int iso8601_to_minutes(const char *in, long *out) {
  iso8601_t f;

  if (iso8601_split(in, &f))
    return -1;
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

typedef struct {
  const char *id;
  int idx;
} chan_idx_t;

static int chan_idx_cmp(const void *a, const void *b) { return strcmp(((const chan_idx_t *)a)->id, ((const chan_idx_t *)b)->id); }

/* -1 if not found */
static int chan_idx_find(const chan_idx_t *idx, int n, const char *id) {
  int lo = 0, hi = n - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(id, idx[mid].id);
    if (c == 0)
      return idx[mid].idx;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return -1;
}

/* windowed->channels copies doc->channels (build_windowed_doc): index stays stable for seen[] mark */
static void emit_metrics(metrics_exporter_t *mx, double now, const bcg_doc_t *doc, const bcg_doc_t *windowed, const bcg_metrics_t *bm,
                          long sched_start, long sched_end, int have_sched) {
  metrics_writer_t w;
  int services_with_events = 0;
  char *seen;
  chan_idx_t *cidx;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;

  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_CONFIGURED, NULL, 1);
  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_UP, NULL, bm->sources_up ? 1 : 0);
  for (unsigned r = 0; r < NET_ERR_COUNT; r++)
    metrics_writer_put(&w, METRICS_ID_BCG_SOURCE_ERRORS_TOTAL, net_err_reason_name((net_err_reason_t)r), bm->source_errors_total[r]);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES, NULL, (uint64_t)doc->channel_count);

  seen = windowed->channel_count > 0 ? calloc((size_t)windowed->channel_count, 1) : NULL;
  cidx = windowed->channel_count > 0 ? malloc(sizeof *cidx * (size_t)windowed->channel_count) : NULL;
  if (cidx) {
    for (int i = 0; i < windowed->channel_count; i++) {
      cidx[i].id = windowed->channels[i].id;
      cidx[i].idx = i;
    }
    qsort(cidx, (size_t)windowed->channel_count, sizeof *cidx, chan_idx_cmp);
  }
  for (int i = 0; i < windowed->programme_count; i++) {
    const bcg_programme_t *pr = &windowed->programmes[i];
    int idx = cidx ? chan_idx_find(cidx, windowed->channel_count, pr->channel_id) : -1;
    if (idx >= 0 && seen && !seen[idx]) {
      seen[idx] = 1;
      services_with_events++;
    }
  }
  free(seen);
  free(cidx);

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

int build_windowed_doc(const bcg_doc_t *src, bcg_doc_t *dst, long now, long window_min,
                        long *out_sched_start, long *out_sched_end, int *out_have_sched) {
  long sched_start = 0, sched_end = 0;
  int have_sched = 0;

  bcg_doc_init(dst);
  for (int i = 0; i < src->channel_count; i++) {
    bcg_channel_t *c = bcg_add_channel(dst);
    if (!c)
      return -1;
    *c = src->channels[i];
  }
  for (int i = 0; i < src->programme_count; i++) {
    const bcg_programme_t *pr = &src->programmes[i];
    bcg_programme_t *out;
    long start_min, end_min;
    if (iso8601_to_minutes(pr->start, &start_min))
      continue;
    end_min = start_min;
    if (pr->stop[0] && !iso8601_to_minutes(pr->stop, &end_min)) {
      /* end_min set */
    }
    if (end_min < now)
      continue;
    if (start_min > now + window_min)
      continue;
    out = bcg_add_programme(dst);
    if (!out)
      return -1;
    *out = *pr;
    if (!have_sched || start_min < sched_start)
      sched_start = start_min;
    if (!have_sched || end_min > sched_end)
      sched_end = end_min;
    have_sched = 1;
  }
  if (out_sched_start)
    *out_sched_start = sched_start;
  if (out_sched_end)
    *out_sched_end = sched_end;
  if (out_have_sched)
    *out_have_sched = have_sched;
  return 0;
}

int load_doc(const config_t *cfg, bcg_doc_t *out) {
  mapping_t map;
  FILE *in;

  in = strcmp(cfg->input_path, "-") ? fopen(cfg->input_path, "r") : stdin;
  if (!in) {
    log_line("cannot open %s", cfg->input_path);
    return -1;
  }
  bcg_doc_init(out);
  if (xmltv_read(in, out)) {
    if (in != stdin)
      fclose(in);
    bcg_doc_free(out);
    return -1;
  }
  if (in != stdin)
    fclose(in);

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

static void reload_doc(const config_t *cfg, bcg_doc_t *doc, bcg_metrics_t *bm, int metrics_on) {
  bcg_doc_t reloaded;
  if (load_doc(cfg, &reloaded)) {
    log_line("reload failed, keeping previous guide");
    if (!metrics_on)
      return;
    bm->sources_up = 0;
    bm->source_errors_total[NET_ERR_FORMAT]++;
    return;
  }
  bcg_doc_free(doc);
  *doc = reloaded;
  log_line("reloaded %d channels, %d programmes from %s / %s", doc->channel_count, doc->programme_count, cfg->input_path, cfg->map_path);
  if (metrics_on)
    bm->sources_up = 1;
}

static void publish_document(mcast_t *m, bitwriter_t *bw, const strrepo_writer_t *sw, unsigned cycles, int compress, bcg_metrics_t *bm, int metrics_on) {
  size_t bits_len, strs_len, cont_len;
  const unsigned char *bits = bitwriter_data(bw, &bits_len);
  const unsigned char *strs = strrepo_writer_data(sw, &strs_len);
  unsigned char *cont;
  unsigned char *wrapped;
  size_t wrapped_len;
  int ok = 0;

  if (container_build(bits, bits_len, strs, strs_len, &cont, &cont_len) != 0) {
    if (metrics_on)
      bm->document_errors_total++;
    return;
  }
  if (wrapper_build(cont, cont_len, compress, &wrapped, &wrapped_len) == 0) {
    ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, cycles % 256, 1, 0, 0, 1, wrapped, wrapped_len) == 0;
    free(wrapped);
  }
  free(cont);
  if (!metrics_on)
    return;
  bm->documents_generated_total++;
  if (ok) {
    bm->publications_total++;
    bm->last_success_time = (double)time(NULL);
  } else {
    bm->publication_errors_total++;
  }
}

int announce_run(const config_t *cfg, metrics_exporter_t *mx) {
  bcg_doc_t doc;
  mcast_t *m;
  unsigned cycles = 0;
  int rc = 0;
  char mcast_txt[80];
  bcg_metrics_t bm;
  accessunit_scratch_t sc;
  int metrics_on = metrics_exporter_enabled(mx);

  memset(&bm, 0, sizeof bm);
  accessunit_scratch_init(&sc);

  if (load_doc(cfg, &doc)) {
    if (metrics_on)
      bm.source_errors_total[NET_ERR_FORMAT]++;
    return 1;
  }
  if (metrics_on)
    bm.sources_up = 1;

  m = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 0);
  if (!m) {
    log_line("cannot open %s:%u for sending", cfg->mcast_group, cfg->mcast_port);
    bcg_doc_free(&doc);
    return 1;
  }

  mcast_describe(cfg, mcast_txt, sizeof mcast_txt);
  log_line("announcing %d channels, %d programmes (window %ldh) on %s every %lds", doc.channel_count, doc.programme_count, cfg->window_hours, mcast_txt, cfg->interval_s);

  while (!signal_stop_requested()) {
    bcg_doc_t windowed;
    bitwriter_t bw;
    strrepo_writer_t sw;
    int nfuu = 0;
    long sched_start, sched_end;
    int have_sched;

    if (signal_reload_requested())
      reload_doc(cfg, &doc, &bm, metrics_on);

    if (build_windowed_doc(&doc, &windowed, now_minutes(), cfg->window_hours * 60, &sched_start, &sched_end, &have_sched)) {
      bcg_doc_free(&windowed);
      if (metrics_on)
        bm.document_errors_total++;
      rc = 1;
      break;
    }

    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    if (accessunit_encode(&sc, &windowed, &bw, &sw, &nfuu)) {
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      bcg_doc_free(&windowed);
      if (metrics_on)
        bm.document_errors_total++;
      rc = 1;
      break;
    }
    publish_document(m, &bw, &sw, cycles, cfg->compress, &bm, metrics_on);
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);

    cycles++;
    if (cfg->verbose)
      log_line("cycle %u sent, %d fragments", cycles, nfuu);
    emit_metrics(mx, mono_seconds(), &doc, &windowed, &bm, sched_start, sched_end, have_sched);
    bcg_doc_free(&windowed);
    sleep_interruptible((double)cfg->interval_s);
  }

  accessunit_scratch_free(&sc);
  mcast_close(m);
  bcg_doc_free(&doc);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return rc;
}
