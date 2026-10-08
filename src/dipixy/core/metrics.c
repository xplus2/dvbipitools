/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "metrics.h"

#include <stdatomic.h>

#include "lib/metrics/render.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"
#include "../ts/capture/capture.h"
#include "../reactor/reactor.h"
#include "../ts/ts_push.h"
#include "../version.h"

static atomic_ullong g_requests_total = 0;
static atomic_ullong g_http_errors_total = 0;

void dipixy_metrics_init(metrics_exporter_t *exp, const config_t *cfg) {
  metrics_exporter_init(exp, METRICS_COMPONENT_XY, cfg->metrics_id, cfg->metrics_sock, (double)cfg->metrics_interval_s);
}

void dipixy_metrics_close(metrics_exporter_t *exp) { metrics_exporter_close(exp); }

void dipixy_metrics_note_request(void) { atomic_fetch_add_explicit(&g_requests_total, 1, memory_order_relaxed); }

void dipixy_metrics_note_http_error(void) { atomic_fetch_add_explicit(&g_http_errors_total, 1, memory_order_relaxed); }

void dipixy_put_queue_metrics(metrics_writer_t *w, void *ctx) {
  ts_push_queue_stats_t st;
  int level = *(const int *)ctx;
  ts_push_queue_stats(&st, mono_seconds());
  metrics_writer_put(w, METRICS_ID_XY_TSPUSH_QUEUE_BYTES, NULL, st.bytes);
  metrics_writer_put(w, METRICS_ID_XY_TSPUSH_QUEUE_MAX_BYTES, NULL, st.max_bytes);
  if (st.ms_known) metrics_writer_put(w, METRICS_ID_XY_TSPUSH_QUEUE_MILLISECONDS, NULL, st.max_ms);
  if (level > 1) {
    metrics_writer_put(w, METRICS_ID_XY_TSPUSH_QUEUE_HIGH_WATERMARK_BYTES, NULL, ts_push_queue_high_watermark());
    metrics_writer_put(w, METRICS_ID_XY_TSPUSH_QUEUE_DROPPED_TOTAL, NULL, ts_push_queue_dropped());
  }
}

static void put_base(metrics_writer_t *w) {
  metrics_writer_put(w, METRICS_ID_XY_CONNECTIONS_TOTAL, NULL, (uint64_t)reactor_connections_total());
  metrics_writer_put(w, METRICS_ID_XY_CONNECTIONS_ACTIVE, NULL, (uint64_t)reactor_connections_active());
  metrics_writer_put(w, METRICS_ID_XY_REQUESTS_TOTAL, NULL, atomic_load_explicit(&g_requests_total, memory_order_relaxed));
  metrics_writer_put(w, METRICS_ID_XY_HTTP_ERRORS_TOTAL, NULL, atomic_load_explicit(&g_http_errors_total, memory_order_relaxed));
  metrics_writer_put(w, METRICS_ID_XY_BYTES_SERVED_TOTAL, NULL, reactor_bytes_served_total());
  metrics_writer_put(w, METRICS_ID_XY_SOURCES_ACTIVE, NULL, (uint64_t)capture_active_count());
  metrics_writer_put(w, METRICS_ID_XY_TSPUSH_SUBS_ACTIVE, NULL, (uint64_t)ts_push_active_count());
}

void dipixy_metrics_push(metrics_exporter_t *exp) {
  metrics_writer_t w;
  if (!metrics_exporter_due(exp, mono_seconds()) || metrics_exporter_begin(exp, &w, TOOL_VERSION)) return;
  put_base(&w);
  metrics_exporter_send(exp, &w);
}

static void put_all(metrics_writer_t *w, void *ctx) {
  const metrics_exporter_t *exp = ctx;
  put_base(w);
  for (int i = 0; i < METRICS_EXTRA_MAX; i++) {
    if (exp->extra[i]) exp->extra[i](w, exp->extra_ctx[i]);
  }
}

int dipixy_metrics_render_prometheus(const metrics_exporter_t *exp, char **out, size_t *out_len) {
  return render_local(METRICS_COMPONENT_XY, put_all, (void *)exp, out, out_len);
}
