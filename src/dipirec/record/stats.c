/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../version.h"
#include "priv.h"

/* clamp: keeps formatted length bounded */
void fmt_dur(double secs, char *buf, size_t n) {
  long s;
  long h;
  long m;
  s = (secs > 0.0) ? (long)secs : 0;
  if (s < 0)
    s = 0;
  if (s > 359999) /* cap 99:59:59 */
    s = 359999;
  h = s / 3600;
  m = (s % 3600) / 60;
  if (h)
    snprintf(buf, n, "%2ld:%02ld:%02ld", h, m, s % 60);
  else
    snprintf(buf, n, "%2ld:%02ld", m, s % 60);
}

/* one same-line stats update, tty only */
void stats_show(const config_t *cfg, double elapsed, unsigned long long bytes, const psi_t *psi) {
  char line[200];
  char dur[16];
  const char *name = "?";
  const char *subs = "-";
  int atr = 0;
  int len;

  if (!log_stderr_is_tty())
    return;
  if (psi) {
    int c;
    int tt = 0;
    int sb = 0;
    const psi_es_t *es = psi_es(psi, &c);
    if (*psi_service_name(psi))
      name = psi_service_name(psi);
    atr = psi_audio_count(psi);
    for (int k = 0; k < c; k++) {
      if (es[k].cls == PID_TELETEXT)
        tt = 1;
      if (es[k].cls == PID_SUBTITLE)
        sb = 1;
    }
    if (tt && sb)
      subs = "ttx+sub";
    else if (tt)
      subs = "txt";
    else if (sb)
      subs = "sub";
    else
      subs = "-";
  }
  fmt_dur(elapsed, dur, sizeof dur);
  len = snprintf(line, sizeof line, "%s %.1fMB %s a=%d s=%s", dur, (double)bytes / 1048576.0, name, atr, subs);
  if (cfg->duration_s && len > 0 && len < (int)sizeof line) {
    double pct = elapsed * 100.0 / (double)cfg->duration_s;
    time_t st = time(NULL) + (time_t)((double)cfg->duration_s - elapsed);
    struct tm tm;
    char stop[16];
    if (pct > 100.0)
      pct = 100.0;
    gmtime_r(&st, &tm);
    strftime(stop, sizeof stop, "%H:%M:%S", &tm);
    snprintf(line + len, sizeof line - (size_t)len, "%.1f%% stop=%s", pct, stop);
  }
  fprintf(stderr, "\r%s\033[K", line);
  fflush(stderr);
}

/* label o<i>: net/rist sinks only, up/retry concept. label rtmp<i>: rtmp targets */
void push_metrics(metrics_exporter_t *mx, const config_t *cfg, const out_sink_t *sinks, int n_sinks,
                   const rtmp_fanout_t *rf, unsigned long long bytes, double start) {
  metrics_writer_t w;
  char label[16];

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_REC_BYTES_TOTAL, NULL, bytes);
  metrics_writer_put(&w, METRICS_ID_REC_ELAPSED_SECONDS, NULL, (uint64_t)(mono_seconds() - start));
  metrics_writer_put(&w, METRICS_ID_REC_DURATION_LIMIT_SECONDS, NULL, cfg->duration_s > 0 ? (uint64_t)cfg->duration_s : 0);
  for (int i = 0; i < n_sinks; i++) {
    if (!sinks[i].net && !sinks[i].rist)
      continue;
    snprintf(label, sizeof label, "o%d", i);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_UP, label, (sinks[i].net_had_error || sinks[i].rist_had_error) ? 0 : 1);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, label, sinks[i].errors_total);
  }
  for (int i = 0; i < rf->n; i++) {
    snprintf(label, sizeof label, "rtmp%d", i);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_UP, label, rf->had_error[i] ? 0 : 1);
    metrics_writer_put(&w, METRICS_ID_REC_OUTPUT_ERRORS_TOTAL, label, rf->errors_total[i]);
  }
  metrics_exporter_send(mx, &w);
}
