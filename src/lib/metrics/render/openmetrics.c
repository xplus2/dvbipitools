/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/metrics/render.h"
#include "lib/metrics/render/render_int.h"

static void render_snapshot_stamps(dstrbuf_t *sb, const store_t *st, double now_mono) {
  int any = 0;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    if (st->slots[i].valid) any = 1;
  }
  if (!any) return;

  add_head(sb, "dvbipi_metrics_snapshot_age_seconds", "gauge", "seconds since this instance's last snapshot was received", 1);
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    const store_slot_t *slot = &st->slots[i];
    if (!slot->valid) continue;
    dstrbuf_add(sb, "dvbipi_metrics_snapshot_age_seconds{");
    append_base_labels(sb, slot);
    dstrbuf_appendf(sb, "} %.3f\n", now_mono - slot->received_mono);
  }

  add_head(sb, "dvbipi_metrics_snapshot_time_seconds", "gauge", "sender's unix time of this instance's last snapshot", 1);
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    const store_slot_t *slot = &st->slots[i];
    if (!slot->valid) continue;
    dstrbuf_add(sb, "dvbipi_metrics_snapshot_time_seconds{");
    append_base_labels(sb, slot);
    dstrbuf_add(sb, "} ");
    dstrbuf_add_u64(sb, slot->snapshot_time);
    dstrbuf_add(sb, "\n");
  }
}

static void render_self_metrics(dstrbuf_t *sb, const store_t *st) {
  uint64_t active = 0;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    if (st->slots[i].valid) active++;
  }

  add_head(sb, "dvbipi_metrics_instances", "gauge", "exporter instances currently tracked", 1);
  add_sample(sb, "dvbipi_metrics_instances", active);

  add_head(sb, "dvbipi_metrics_snapshots_received_total", "counter", "snapshots accepted and stored", 1);
  add_sample(sb, "dvbipi_metrics_snapshots_received_total", st->stats.snapshots_received_total);

  add_head(sb, "dvbipi_metrics_snapshots_rejected_total", "counter", "snapshots rejected by reason", 1);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"malformed\"}", st->stats.snapshots_rejected_malformed);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"stale\"}", st->stats.snapshots_rejected_stale);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"full\"}", st->stats.snapshots_rejected_full);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"version\"}", st->stats.snapshots_rejected_version);
  add_sample(sb, "dvbipi_metrics_snapshots_rejected_total{reason=\"toolarge\"}", st->stats.snapshots_rejected_toolarge);

  add_head(sb, "dvbipi_metrics_snapshots_incomplete_total", "counter", "snapshots discarded because a piece of them was lost", 1);
  add_sample(sb, "dvbipi_metrics_snapshots_incomplete_total", st->stats.snapshots_incomplete);

  add_head(sb, "dvbipi_metrics_parts_orphaned_total", "counter", "stray snapshot pieces dropped", 1);
  add_sample(sb, "dvbipi_metrics_parts_orphaned_total", st->stats.parts_orphaned);

  add_head(sb, "dvbipi_metrics_http_requests_total", "counter", "/metrics HTTP requests by response status", 1);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"200\"}", st->stats.http_requests_200);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"401\"}", st->stats.http_requests_401);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"404\"}", st->stats.http_requests_404);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"405\"}", st->stats.http_requests_405);
  add_sample(sb, "dvbipi_metrics_http_requests_total{status=\"500\"}", st->stats.http_requests_500);
}

int render_openmetrics(const store_t *st, double now_mono, char **out, size_t *out_len) {
  dstrbuf_t sb;
  dstrbuf_init(&sb);
  if (render_series(&sb, st, 1, 1)) {
    free(sb.buf);
    return -1;
  }
  render_snapshot_stamps(&sb, st, now_mono);
  render_self_metrics(&sb, st);
  dstrbuf_add(&sb, "# EOF\n");
  if (!sb.buf) return -1;
  *out = sb.buf;
  *out_len = sb.len;
  return 0;
}

static int local_flush(void *ctx, const unsigned char *buf, size_t len) {
  store_ingest(ctx, buf, len, 0.0, 0);
  return 0;
}

int render_local(metrics_component_t component, metrics_extra_fn fill, void *ctx, char **out, size_t *out_len) {
  store_t st;
  metrics_writer_t w;
  metrics_hdr_t hdr;
  dstrbuf_t sb;
  size_t len;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = component;
  bufcpy(hdr.metrics_id, sizeof hdr.metrics_id, "local");
  hdr.process_start_time = 1;
  hdr.sequence = 1;
  if (metrics_writer_begin(&w, &hdr)) return -1;
  store_init(&st);
  w.flush = local_flush;
  w.flush_ctx = &st;
  fill(&w, ctx);
  len = metrics_writer_finish(&w);
  if (!len) {
    store_free(&st);
    return -1;
  }
  store_ingest(&st, w.buf, len, 0.0, 0);
  dstrbuf_init(&sb);
  if (render_series(&sb, &st, 0, 0)) {
    free(sb.buf);
    store_free(&st);
    return -1;
  }
  store_free(&st);
  *out = sb.buf;
  *out_len = sb.len;
  return 0;
}
