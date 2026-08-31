/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "store.h"

const char *metrics_component_name(metrics_component_t c) {
  switch (c) {
  case METRICS_COMPONENT_TVHEAD:
    return "tvhead";
  case METRICS_COMPONENT_RADIOHEAD:
    return "radiohead";
  case METRICS_COMPONENT_SDS:
    return "sds";
  case METRICS_COMPONENT_BCG:
    return "bcg";
  case METRICS_COMPONENT_SRT:
    return "srt";
  case METRICS_COMPONENT_RIST:
    return "rist";
  case METRICS_COMPONENT_REC:
    return "rec";
  case METRICS_COMPONENT_DESCRAMBLE:
    return "descramble";
  case METRICS_COMPONENT_CAM378:
    return "cam378";
  case METRICS_COMPONENT_FCCRET:
    return "fccret";
  case METRICS_COMPONENT_XY:
    return "xy";
  default:
    return "unknown";
  }
}

void store_init(store_t *st) {
  memset(st, 0, sizeof *st);
}

static store_slot_t *find_slot(store_t *st, metrics_component_t component, const char *metrics_id) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    store_slot_t *s = &st->slots[i];
    if (s->used && s->component == component && !strcmp(s->metrics_id, metrics_id))
      return s;
  }
  return NULL;
}

static store_slot_t *find_free_slot(store_t *st) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++)
    if (!st->slots[i].used)
      return &st->slots[i];
  return NULL;
}

static void note_entries_dropped(store_stats_t *stats, int *over_cap_logged, metrics_component_t component, const char *metrics_id, int verbose) {
  if (!*over_cap_logged) {
    if (verbose)
      log_line("dipimetrics: snapshot for %s/%s has more than %d entries, dropping the rest", metrics_component_name(component), metrics_id, STORE_MAX_ENTRIES);
    *over_cap_logged = 1;
  }
  stats->snapshot_entries_dropped++;
}

static void ingest_entries(metrics_reader_t *r, store_slot_t *slot, store_stats_t *stats, metrics_component_t component, const char *metrics_id, int verbose) {
  int over_cap_logged = 0;
  for (;;) {
    metrics_id_t id;
    char label[METRICS_LABEL_MAX + 1];
    uint64_t value;
    int rc = metrics_reader_next(r, &id, label, sizeof label, &value);
    if (rc == 0)
      return;
    if (rc < 0) {
      if (verbose)
        log_line("dipimetrics: truncated entry, discarding rest of snapshot for %s/%s", metrics_component_name(component), metrics_id);
      return;
    }
    if (slot->entry_count < STORE_MAX_ENTRIES) {
      stored_entry_t *e = &slot->entries[slot->entry_count++];
      e->id = id;
      bufcpy(e->label, sizeof e->label, label);
      e->value = value;
    } else {
      note_entries_dropped(stats, &over_cap_logged, component, metrics_id, verbose);
    }
  }
}

void store_ingest(store_t *st, const unsigned char *buf, size_t len, double now_mono, int verbose) {
  metrics_reader_t r;
  metrics_hdr_t hdr;
  store_slot_t *slot;
  int restarted;

  if (len > METRICS_MAX_SNAPSHOT_BYTES) {
    st->stats.snapshots_rejected_malformed++;
    if (verbose)
      log_line("dipimetrics: rejected malformed snapshot (%zu bytes)", len);
    return;
  }
  if (len >= 1 && buf[0] != METRICS_PROTO_VERSION) {
    st->stats.snapshots_rejected_version++;
    if (verbose)
      log_line("dipimetrics: rejected snapshot with unsupported protocol version %u", (unsigned)buf[0]);
    return;
  }
  if (metrics_reader_init(&r, buf, len, &hdr)) {
    st->stats.snapshots_rejected_malformed++;
    if (verbose)
      log_line("dipimetrics: rejected malformed snapshot (%zu bytes)", len);
    return;
  }

  slot = find_slot(st, hdr.component, hdr.metrics_id);
  restarted = slot && hdr.process_start_time != slot->process_start_time;
  if (slot && !restarted && hdr.sequence <= slot->sequence) {
    st->stats.snapshots_rejected_stale++;
    if (verbose)
      log_line("dipimetrics: dropped stale snapshot for %s/%s (seq %llu <= %llu)", metrics_component_name(hdr.component), hdr.metrics_id,
                (unsigned long long)hdr.sequence, (unsigned long long)slot->sequence);
    return;
  }
  if (!slot) {
    slot = find_free_slot(st);
    if (!slot) {
      st->stats.snapshots_rejected_full++;
      if (verbose)
        log_line("dipimetrics: dropped snapshot from new instance %s/%s, store full (%d slots)", metrics_component_name(hdr.component), hdr.metrics_id,STORE_MAX_INSTANCES);
      return;
    }
  }

  slot->used = 1;
  slot->component = hdr.component;
  bufcpy(slot->metrics_id, sizeof slot->metrics_id, hdr.metrics_id);
  slot->process_start_time = hdr.process_start_time;
  slot->sequence = hdr.sequence;
  slot->snapshot_time = hdr.snapshot_time;
  slot->received_mono = now_mono;
  slot->entry_count = 0;
  st->stats.snapshots_received_total++;

  ingest_entries(&r, slot, &st->stats, hdr.component, hdr.metrics_id, verbose);
}

void store_reap_expired(store_t *st, double now_mono, double expiry_s) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    store_slot_t *s = &st->slots[i];
    if (s->used && now_mono - s->received_mono > expiry_s)
      s->used = 0;
  }
}
