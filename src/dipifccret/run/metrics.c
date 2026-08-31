/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <time.h>

#include "lib/metrics/export.h"
#include "lib/helper/signal.h"
#include "../version.h"
#include "run.h"

static void push_metrics(metrics_ctx_t *mc) {
  metrics_writer_t w;
  size_t cap;
  size_t active_channels = 0;

  if (!metrics_exporter_due(mc->mx, mono_seconds()) || metrics_exporter_begin(mc->mx, &w, TOOL_VERSION))
    return;

  cap = channel_table_capacity(mc->channels);
  for (size_t i = 0; i < cap; i++)
    if (channel_table_at(mc->channels, i))
      active_channels++;
  metrics_writer_put(&w, METRICS_ID_FCC_CHANNELS_ACTIVE, NULL, active_channels);

  if (mc->ret)
    metrics_writer_put(&w, METRICS_ID_FCC_RET_CLIENTS_ACTIVE, NULL, ret_ctx_active_clients(mc->ret));

  if (mc->bursts) {
    burst_table_metrics_t bm;
    burst_table_get_metrics(mc->bursts, &bm);
    metrics_writer_put(&w, METRICS_ID_FCC_BURSTS_ACTIVE, NULL, bm.bursts_active);
    metrics_writer_put(&w, METRICS_ID_FCC_BYTES_RETRANSMITTED_TOTAL, NULL, bm.bytes_retransmitted_total);
    metrics_writer_put(&w, METRICS_ID_FCC_NACKS_TOTAL, NULL, bm.nacks_total);
    metrics_writer_put(&w, METRICS_ID_FCC_CONGESTION_ADAPTATIONS_TOTAL, NULL, bm.congestion_adaptations_total);
  }

  metrics_exporter_send(mc->mx, &w);
}

void *metrics_thread_main(void *arg) {
  metrics_ctx_t *mc = arg;
  struct timespec tick = {0, 200 * 1000 * 1000}; /* 200ms: stop signal noticed promptly */

  while (!signal_stop_requested()) {
    push_metrics(mc);
    nanosleep(&tick, NULL);
  }
  return NULL;
}
