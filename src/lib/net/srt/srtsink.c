/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "srtcommon.h"
#include "srtout.h"
#include "srtsink.h"

struct srtsink {
  srtout_t *o;
};

static srtgroup_mode_t map_group_mode(srtsink_group_mode_t m) {
  switch (m) {
  case SRTSINK_GROUP_BROADCAST:
    return SRTGROUP_BROADCAST;
  case SRTSINK_GROUP_BACKUP:
    return SRTGROUP_BACKUP;
  default:
    return SRTGROUP_NONE;
  }
}

srtsink_t *srtsink_open(const srtsink_cfg_t *cfg) {
  srtsink_t *r;
  srtout_cfg_t oc;

  if (cfg->npeers <= 0 || cfg->npeers > SRTSINK_MAX_PEERS)
    return NULL;

  memset(&oc, 0, sizeof oc);
  for (int i = 0; i < cfg->npeers; i++) {
    oc.peers[i].host = cfg->peers[i].host;
    oc.peers[i].port = cfg->peers[i].port;
  }
  oc.npeers = cfg->npeers;
  oc.group_mode = map_group_mode(cfg->group_mode);
  oc.opts.passphrase = cfg->passphrase;
  oc.opts.pbkeylen = cfg->pbkeylen;
  oc.opts.streamid = cfg->streamid;
  oc.opts.packetfilter = cfg->packetfilter;
  oc.opts.latency_ms = cfg->latency_ms;
  oc.verbose = cfg->verbose;
  oc.mx = cfg->mx;
  oc.tool_version = cfg->tool_version;
  oc.safety_mult = cfg->safety_mult;
  oc.queue_metrics = cfg->queue_metrics;

  r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->o = srtout_open(&oc);
  if (!r->o) {
    free(r);
    return NULL;
  }
  return r;
}

void srtsink_service(srtsink_t *r, srtsink_status_t *out) {
  srtout_status_t st;
  srtout_service(r->o, &st);
  out->connected = st.connected;
}

void srtsink_write(srtsink_t *r, const unsigned char *buf, size_t n) { srtout_write(r->o, buf, n); }

void srtsink_close(srtsink_t *r) {
  if (!r) return;
  srtout_close(r->o);
  free(r);
}

void srtsink_put_queue_metrics(metrics_writer_t *w, void *ctx) {
  const srtsink_queue_ctx_t *q = ctx;

  for (unsigned i = 0; i < q->n; i++) {
    srtout_queue_stats_t st;
    if (!q->sinks[i]) continue;
    srtout_queue_stats(q->sinks[i]->o, &st);
    metrics_writer_put(w, METRICS_ID_SRT_SENDER_QUEUE_CHUNKS, st.peer_label, (uint64_t)st.chunks);
    metrics_writer_put(w, METRICS_ID_SRT_SENDER_QUEUE_CAPACITY_CHUNKS, st.peer_label, (uint64_t)st.capacity);
    if (q->queue_metrics > 1) {
      metrics_writer_put(w, METRICS_ID_SRT_SENDER_QUEUE_HIGH_WATERMARK_CHUNKS, st.peer_label, (uint64_t)st.high_watermark);
      metrics_writer_put(w, METRICS_ID_SRT_SENDER_QUEUE_DROPPED_CHUNKS_TOTAL, st.peer_label, st.dropped);
    }
  }
}
