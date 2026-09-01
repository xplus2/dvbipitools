/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "metrics.h"

#include <stdatomic.h>
#include <string.h>

#include "lib/helper/signal.h"

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

void dipixy_metrics_push(metrics_exporter_t *exp) {
  metrics_writer_t w;
  if (!metrics_exporter_due(exp, mono_seconds()) || metrics_exporter_begin(exp, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_XY_CONNECTIONS_TOTAL, NULL, (uint64_t)reactor_connections_total());
  metrics_writer_put(&w, METRICS_ID_XY_CONNECTIONS_ACTIVE, NULL, (uint64_t)reactor_connections_active());
  metrics_writer_put(&w, METRICS_ID_XY_REQUESTS_TOTAL, NULL, atomic_load_explicit(&g_requests_total, memory_order_relaxed));
  metrics_writer_put(&w, METRICS_ID_XY_HTTP_ERRORS_TOTAL, NULL, atomic_load_explicit(&g_http_errors_total, memory_order_relaxed));
  metrics_writer_put(&w, METRICS_ID_XY_BYTES_SERVED_TOTAL, NULL, reactor_bytes_served_total());
  metrics_writer_put(&w, METRICS_ID_XY_SOURCES_ACTIVE, NULL, (uint64_t)capture_active_count());
  metrics_writer_put(&w, METRICS_ID_XY_TSPUSH_SUBS_ACTIVE, NULL, (uint64_t)ts_push_active_count());
  metrics_exporter_send(exp, &w);
}

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  int truncated;
} strbuf_t;

static void sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  b->truncated = 0;
  if (cap)
    buf[0] = '\0';
}

static void sb_add(strbuf_t *b, const char *s) {
  size_t n = strlen(s);
  size_t room = b->cap > b->len + 1 ? b->cap - b->len - 1 : 0;
  if (n > room) {
    n = room;
    b->truncated = 1;
  }
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

static void sb_add_u64(strbuf_t *b, uint64_t v) {
  char tmp[20], rev[21];
  size_t n = 0;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + v % 10);
      v /= 10;
    }
  }
  for (size_t i = 0; i < n; i++)
    rev[i] = tmp[n - 1 - i];
  rev[n] = '\0';
  sb_add(b, rev);
}

int dipixy_metrics_render_prometheus(char **out, size_t *out_len) {
  static _Thread_local char buf[2048];
  strbuf_t b;

  sb_init(&b, buf, sizeof buf);
  sb_add(&b, "# HELP dvbipi_xy_connections_total connections accepted, every protocol\n"
             "# TYPE dvbipi_xy_connections_total counter\n"
             "dvbipi_xy_connections_total ");
  sb_add_u64(&b, (uint64_t)reactor_connections_total());
  sb_add(&b, "\n# HELP dvbipi_xy_connections_active connections currently open\n"
             "# TYPE dvbipi_xy_connections_active gauge\n"
             "dvbipi_xy_connections_active ");
  sb_add_u64(&b, (uint64_t)reactor_connections_active());
  sb_add(&b, "\n# HELP dvbipi_xy_requests_total HTTP requests dispatched\n"
             "# TYPE dvbipi_xy_requests_total counter\n"
             "dvbipi_xy_requests_total ");
  sb_add_u64(&b, atomic_load_explicit(&g_requests_total, memory_order_relaxed));
  sb_add(&b, "\n# HELP dvbipi_xy_http_errors_total HTTP responses with a 4xx/5xx status\n"
             "# TYPE dvbipi_xy_http_errors_total counter\n"
             "dvbipi_xy_http_errors_total ");
  sb_add_u64(&b, atomic_load_explicit(&g_http_errors_total, memory_order_relaxed));
  sb_add(&b, "\n# HELP dvbipi_xy_bytes_served_total wire bytes queued to clients, headers and body\n"
             "# TYPE dvbipi_xy_bytes_served_total counter\n"
             "dvbipi_xy_bytes_served_total ");
  sb_add_u64(&b, reactor_bytes_served_total());
  sb_add(&b, "\n# HELP dvbipi_xy_sources_active distinct multicast joins currently open\n"
             "# TYPE dvbipi_xy_sources_active gauge\n"
             "dvbipi_xy_sources_active ");
  sb_add_u64(&b, (uint64_t)capture_active_count());
  sb_add(&b, "\n# HELP dvbipi_xy_tspush_subscribers_active raw TS push clients currently attached\n"
             "# TYPE dvbipi_xy_tspush_subscribers_active gauge\n"
             "dvbipi_xy_tspush_subscribers_active ");
  sb_add_u64(&b, (uint64_t)ts_push_active_count());
  sb_add(&b, "\n");

  if (b.truncated) /* fixed template, should never truncate at this cap */
    return -1;
  *out = buf;
  *out_len = b.len;
  return 0;
}
