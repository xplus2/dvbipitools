/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_STORE_H
#define DIPIMETRICS_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "lib/metrics/protocol.h"

#define STORE_MAX_INSTANCES 64
#define STORE_MAX_BLOB_BYTES (256 * 1024)

typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} store_blob_t;

typedef struct {
  int used;
  int valid;
  metrics_component_t component;
  char metrics_id[METRICS_ID_MAX];
  uint64_t process_start_time;
  uint64_t sequence;
  uint64_t snapshot_time; /* unix seconds, from sender */
  double received_mono;   /* mono_seconds() at last complete snapshot, for age/expiry */
  double activity_mono;
  unsigned version;
  store_blob_t live;
  store_blob_t stage;
  int staging;
  uint64_t stage_process_start;
  uint64_t stage_sequence;
  unsigned stage_next;
} store_slot_t;

typedef struct {
  uint64_t snapshots_received_total;
  uint64_t snapshots_rejected_malformed;
  uint64_t snapshots_rejected_stale;
  uint64_t snapshots_rejected_full;
  uint64_t snapshots_rejected_version;
  uint64_t snapshots_rejected_toolarge;
  uint64_t snapshots_incomplete;
  uint64_t parts_orphaned;
  uint64_t http_requests_200;
  uint64_t http_requests_404;
} store_stats_t;

typedef struct {
  store_slot_t slots[STORE_MAX_INSTANCES];
  store_stats_t stats;
} store_t;

void store_init(store_t *st);
void store_free(store_t *st);

/* validate a datagram and stage/commit it to a slot(component, metrics_id) .
   v2 parts commit atomically at last part. rejects + logs (if -v) and counts into st->stats.
   changed process_start_time is treated as an exporter restart */
void store_ingest(store_t *st, const unsigned char *buf, size_t len, double now_mono, int verbose);

/* frees slots idle longer than expiry_s and their buffers */
void store_reap_expired(store_t *st, double now_mono, double expiry_s);

const char *metrics_component_name(metrics_component_t c);

#endif
