/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_STORE_H
#define DIPIMETRICS_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "lib/metrics/protocol.h"

#define STORE_MAX_INSTANCES 64
/* (METRICS_MAX_SNAPSHOT_BYTES - METRICS_HDR_LEN) / min-entry-size(11), rounded up */
#define STORE_MAX_ENTRIES 400

typedef struct {
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
} stored_entry_t;

typedef struct {
  int used;
  metrics_component_t component;
  char metrics_id[METRICS_ID_MAX];
  uint64_t process_start_time;
  uint64_t sequence;
  uint64_t snapshot_time; /* unix seconds, from the sender */
  double received_mono;   /* mono_seconds() at receipt, for age/expiry */
  stored_entry_t entries[STORE_MAX_ENTRIES];
  int entry_count;
} store_slot_t;

typedef struct {
  uint64_t snapshots_received_total;
  uint64_t snapshots_rejected_malformed;
  uint64_t snapshots_rejected_stale;
  uint64_t snapshots_rejected_full;
  uint64_t snapshots_rejected_version;
  uint64_t http_requests_200;
  uint64_t http_requests_404;
} store_stats_t;

typedef struct {
  store_slot_t slots[STORE_MAX_INSTANCES];
  store_stats_t stats;
} store_t;

void store_init(store_t *st);

/* decodes+validates one datagram and updates (or creates) a (component, metrics_id) slot.
   rejects + logs (if -v) and counts into st->stats. changed process_start_time is treated
   as an exporter restart */
void store_ingest(store_t *st, const unsigned char *buf, size_t len, double now_mono, int verbose);

/* frees slots idle longer than expiry_s so a new instance can reuse them */
void store_reap_expired(store_t *st, double now_mono, double expiry_s);

const char *metrics_component_name(metrics_component_t c);

#endif
