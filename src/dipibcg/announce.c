/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "announce.h"
#include "container.h"
#include "lib/bim/accessunit.h"
#include "lib/bim/bitwriter.h"
#include "lib/bim/strrepo.h"
#include "lib/log.h"
#include "lib/metrics/export.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/net/netconnect.h"
#include "lib/signal.h"
#include "lib/tva/bcg_doc.h"
#include "lib/tva/mapping.h"
#include "lib/tva/xmltv.h"
#include "version.h"

/* EN 300 468 annex C, same formula as lib/bim/codec.c's dvbDateTimeCodec */
static long date_to_mjd(int y, int mo, int d) {
  int yy = mo <= 2 ? y - 1 : y;
  int mm = mo <= 2 ? mo + 12 : mo;
  return 14956L + d + (long)((yy - 1900) * 365.25) + (long)((mm + 1) * 30.6001);
}

static int all_digits(const char *s, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

/* "YYYY-MM-DDTHH:MM:SS[Z|+HH:MM|-HH:MM]" -> minutes since the MJD epoch, UTC-normalized */
static int iso8601_to_minutes(const char *in, long *out) {
  size_t l = strlen(in);
  int y, mo, d, h, mi, off_min = 0;
  long mjd, total;

  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) ||
      !all_digits(in + 11, 2) || !all_digits(in + 14, 2))
    return -1;
  y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  mo = (in[5] - '0') * 10 + (in[6] - '0');
  d = (in[8] - '0') * 10 + (in[9] - '0');
  h = (in[11] - '0') * 10 + (in[12] - '0');
  mi = (in[14] - '0') * 10 + (in[15] - '0');

  if (l > 19) {
    const char *tail = in + 19;
    if (*tail == '+' || *tail == '-') {
      if (strlen(tail) >= 6 && tail[3] == ':' && all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
        int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
        int om = (tail[4] - '0') * 10 + (tail[5] - '0');
        off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
      } else {
        return -1;
      }
    }
  }

  mjd = date_to_mjd(y, mo, d);
  total = mjd * 1440L + h * 60L + mi - off_min;
  *out = total;
  return 0;
}

static long now_minutes(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  return date_to_mjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 1440L + tm.tm_hour * 60L + tm.tm_min;
}

/* MJD epoch (date_to_mjd 1970-01-01) is day 40587 */
static long minutes_to_unix(long mjd_minutes) {
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

/* windowed->channels is a copy of doc->channels (build_windowed_doc), so
   bcg_find_channel() index into it is stable for the seen[] mark */
static void emit_metrics(metrics_exporter_t *mx, double now, const bcg_doc_t *doc, const bcg_doc_t *windowed, const bcg_metrics_t *bm) {
  metrics_writer_t w;
  unsigned r;
  int i, services_with_events = 0;
  long sched_start = 0, sched_end = 0;
  int have_sched = 0;
  char *seen;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;

  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_CONFIGURED, NULL, 1);
  metrics_writer_put(&w, METRICS_ID_BCG_SOURCES_UP, NULL, bm->sources_up ? 1 : 0);
  for (r = 0; r < NET_ERR_COUNT; r++)
    metrics_writer_put(&w, METRICS_ID_BCG_SOURCE_ERRORS_TOTAL, net_err_reason_name((net_err_reason_t)r), bm->source_errors_total[r]);
  metrics_writer_put(&w, METRICS_ID_BCG_SERVICES, NULL, (uint64_t)doc->channel_count);

  seen = windowed->channel_count > 0 ? calloc((size_t)windowed->channel_count, 1) : NULL;
  for (i = 0; i < windowed->programme_count; i++) {
    const bcg_programme_t *pr = &windowed->programmes[i];
    const bcg_channel_t *c = bcg_find_channel(windowed, pr->channel_id);
    long s, e;
    if (c && seen) {
      int idx = (int)(c - windowed->channels);
      if (!seen[idx]) {
        seen[idx] = 1;
        services_with_events++;
      }
    }
    if (!iso8601_to_minutes(pr->start, &s)) {
      e = s;
      if (pr->stop[0])
        iso8601_to_minutes(pr->stop, &e);
      if (!have_sched || s < sched_start)
        sched_start = s;
      if (!have_sched || e > sched_end)
        sched_end = e;
      have_sched = 1;
    }
  }
  free(seen);

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

static int build_windowed_doc(const bcg_doc_t *src, bcg_doc_t *dst, long now, long window_min) {
  int i;
  bcg_doc_init(dst);
  for (i = 0; i < src->channel_count; i++) {
    bcg_channel_t *c = bcg_add_channel(dst);
    if (!c)
      return -1;
    *c = src->channels[i];
  }
  for (i = 0; i < src->programme_count; i++) {
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
  }
  return 0;
}

static int load_doc(const config_t *cfg, bcg_doc_t *out) {
  mapping_t map;
  FILE *in;
  int i;

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
  for (i = 0; i < out->channel_count; i++) {
    bcg_channel_t *c = &out->channels[i];
    char uri[BCG_ID_LEN];
    unsigned tsid, onid, sid;
    if (!mapping_lookup(&map, c->id, uri, sizeof uri, &tsid, &onid, &sid)) {
      snprintf(c->uri, sizeof c->uri, "%s", uri);
      c->tsid = tsid;
      c->onid = onid;
      c->sid = sid;
    }
  }
  mapping_free(&map);
  return 0;
}

int announce_run(const config_t *cfg, metrics_exporter_t *mx) {
  bcg_doc_t doc;
  mcast_t *m;
  unsigned cycles = 0;
  int rc = 0;
  char mcast_txt[80];
  bcg_metrics_t bm;
  int metrics_on = metrics_exporter_enabled(mx);

  memset(&bm, 0, sizeof bm);

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

    if (signal_reload_requested()) {
      bcg_doc_t reloaded;
      if (load_doc(cfg, &reloaded)) {
        log_line("reload failed, keeping previous guide");
        if (metrics_on) {
          bm.sources_up = 0;
          bm.source_errors_total[NET_ERR_FORMAT]++;
        }
      } else {
        bcg_doc_free(&doc);
        doc = reloaded;
        log_line("reloaded %d channels, %d programmes from %s / %s", doc.channel_count, doc.programme_count, cfg->input_path, cfg->map_path);
        if (metrics_on)
          bm.sources_up = 1;
      }
    }

    if (build_windowed_doc(&doc, &windowed, now_minutes(), cfg->window_hours * 60)) {
      bcg_doc_free(&windowed);
      if (metrics_on)
        bm.document_errors_total++;
      rc = 1;
      break;
    }

    bitwriter_init(&bw);
    strrepo_writer_init(&sw);
    if (accessunit_encode(&windowed, &bw, &sw, &nfuu)) {
      bitwriter_free(&bw);
      strrepo_writer_free(&sw);
      bcg_doc_free(&windowed);
      if (metrics_on)
        bm.document_errors_total++;
      rc = 1;
      break;
    }
    {
      size_t bits_len, strs_len, cont_len;
      const unsigned char *bits = bitwriter_data(&bw, &bits_len);
      const unsigned char *strs = strrepo_writer_data(&sw, &strs_len);
      unsigned char *cont;
      if (container_build(bits, bits_len, strs, strs_len, &cont, &cont_len) == 0) {
        int ok = dvbstp_send_segment(m, DVBSTP_PAYLOAD_BCG_DATA_CONTAINER, 1, (unsigned)(cycles % 256), 0, 0, 1, cont, cont_len) == 0;
        free(cont);
        if (metrics_on) {
          bm.documents_generated_total++;
          if (ok) {
            bm.publications_total++;
            bm.last_success_time = (double)time(NULL);
          } else {
            bm.publication_errors_total++;
          }
        }
      } else if (metrics_on) {
        bm.document_errors_total++;
      }
    }
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);

    cycles++;
    if (cfg->verbose)
      log_line("cycle %u sent, %d fragments", cycles, nfuu);
    emit_metrics(mx, mono_seconds(), &doc, &windowed, &bm);
    bcg_doc_free(&windowed);
    sleep_interruptible((double)cfg->interval_s);
  }

  mcast_close(m);
  bcg_doc_free(&doc);
  log_line("stopped after %u cycle%s", cycles, cycles == 1 ? "" : "s");
  return rc;
}
