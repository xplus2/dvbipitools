/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "store.h"

const char *metrics_component_name(metrics_component_t c) {
  switch (c) {
    case METRICS_COMPONENT_TVHEAD:      return "tvhead";
    case METRICS_COMPONENT_RADIOHEAD:   return "radiohead";
    case METRICS_COMPONENT_SDS:         return "sds";
    case METRICS_COMPONENT_BCG:         return "bcg";
    case METRICS_COMPONENT_SRT:         return "srt";
    case METRICS_COMPONENT_RIST:        return "rist";
    case METRICS_COMPONENT_REC:         return "rec";
    case METRICS_COMPONENT_DESCRAMBLE:  return "descramble";
    case METRICS_COMPONENT_CAM378:      return "cam378";
    case METRICS_COMPONENT_FCCRET:      return "fccret";
    case METRICS_COMPONENT_XY:          return "xy";
    default:                            return "unknown";
  }
}

void store_init(store_t *st) {
  memset(st, 0, sizeof *st);
}

static void blob_free(store_blob_t *b) {
  free(b->data);
  memset(b, 0, sizeof *b);
}

static int blob_append(store_blob_t *b, const unsigned char *data, size_t n) {
  if (growbuf_reserve((void **)&b->data, &b->cap, 1, b->len + n, 4096)) return -1;
  if (n) memcpy(b->data + b->len, data, n);
  b->len += n;
  return 0;
}

static void release_slot(store_slot_t *s) {
  blob_free(&s->live);
  blob_free(&s->stage);
  memset(s, 0, sizeof *s);
}

void store_free(store_t *st) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) release_slot(&st->slots[i]);
}

static store_slot_t *find_slot(store_t *st, metrics_component_t component, const char *metrics_id) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    store_slot_t *s = &st->slots[i];
    if (s->used && s->component == component && !strcmp(s->metrics_id, metrics_id)) return s;
  }
  return NULL;
}

static store_slot_t *find_free_slot(store_t *st) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) if (!st->slots[i].used) return &st->slots[i];
  return NULL;
}

static void discard_stage(store_slot_t *s, store_stats_t *stats, int count_incomplete) {
  if (s->staging && count_incomplete) stats->snapshots_incomplete++;
  s->staging = 0;
  s->stage.len = 0;
}

static void release_if_empty(store_slot_t *s) {
  if (!s->valid && !s->staging) release_slot(s);
}

static int body_valid(unsigned version, const unsigned char *body, size_t len) {
  metrics_reader_t r;
  metrics_id_t id;
  const char *label;
  size_t label_len;
  uint64_t value;
  int rc;
  metrics_reader_init_body(&r, version, body, len);
  while ((rc = metrics_reader_next_ref(&r, &id, &label, &label_len, &value)) == 1);
  return rc;
}

static void commit_stage(store_slot_t *s, store_stats_t *stats, const metrics_hdr_t *hdr, double now_mono) {
  store_blob_t t = s->live;
  s->live = s->stage;
  s->stage = t;
  s->stage.len = 0;
  s->staging = 0;
  s->valid = 1;
  s->version = hdr->proto_version;
  s->process_start_time = s->stage_process_start;
  s->sequence = s->stage_sequence;
  s->snapshot_time = hdr->snapshot_time;
  s->received_mono = now_mono;
  stats->snapshots_received_total++;
}

void store_ingest(store_t *st, const unsigned char *buf, size_t len, double now_mono, int verbose) {
  metrics_reader_t r;
  metrics_hdr_t hdr;
  store_slot_t *slot;
  const unsigned char *body;
  size_t body_len;

  if (len > METRICS_MAX_SNAPSHOT_BYTES) {
    st->stats.snapshots_rejected_malformed++;
    if (verbose) log_line("dipimetrics: rejected malformed snapshot (%zu bytes)", len);
    return;
  }
  if (len >= 1 && buf[0] != METRICS_PROTO_V1 && buf[0] != METRICS_PROTO_VERSION) {
    st->stats.snapshots_rejected_version++;
    if (verbose) log_line("dipimetrics: rejected snapshot with unsupported protocol version %u", (unsigned)buf[0]);
    return;
  }
  if (metrics_reader_init(&r, buf, len, &hdr)) {
    st->stats.snapshots_rejected_malformed++;
    if (verbose) log_line("dipimetrics: rejected malformed snapshot (%zu bytes)", len);
    return;
  }
  body = buf + METRICS_HDR_LEN;
  body_len = len - METRICS_HDR_LEN;
  slot = find_slot(st, hdr.component, hdr.metrics_id);
  if (hdr.part == 0) {
    int same_live = slot && slot->valid && hdr.process_start_time == slot->process_start_time;
    int same_stage = slot && slot->staging && hdr.process_start_time == slot->stage_process_start;
    if ((same_live && hdr.sequence <= slot->sequence) || (same_stage && hdr.sequence <= slot->stage_sequence)) {
      st->stats.snapshots_rejected_stale++;
      if (verbose)
        log_line("dipimetrics: dropped stale snapshot for %s/%s (seq %llu)", metrics_component_name(hdr.component), hdr.metrics_id, (unsigned long long)hdr.sequence);
      return;
    }
    if (!slot) {
      slot = find_free_slot(st);
      if (!slot) {
        st->stats.snapshots_rejected_full++;
        if (verbose)
          log_line("dipimetrics: dropped snapshot from new instance %s/%s, store full (%d slots)", metrics_component_name(hdr.component), hdr.metrics_id, STORE_MAX_INSTANCES);
        return;
      }
      slot->used = 1;
      slot->component = hdr.component;
      bufcpy(slot->metrics_id, sizeof slot->metrics_id, hdr.metrics_id);
    }
    discard_stage(slot, &st->stats, 1);
    slot->staging = 1;
    slot->stage_process_start = hdr.process_start_time;
    slot->stage_sequence = hdr.sequence;
    slot->stage_next = 0;
  } else if (!slot || !slot->staging || hdr.process_start_time != slot->stage_process_start || hdr.sequence != slot->stage_sequence || hdr.part != slot->stage_next) {
    if (slot && slot->staging && hdr.process_start_time == slot->stage_process_start && hdr.sequence == slot->stage_sequence) {
      discard_stage(slot, &st->stats, 1);
      release_if_empty(slot);
    }
    st->stats.parts_orphaned++;
    if (verbose) log_line("dipimetrics: dropped orphan part %u for %s/%s", (unsigned)hdr.part, metrics_component_name(hdr.component), hdr.metrics_id);
    return;
  }

  if (body_valid(hdr.proto_version, body, body_len)) {
    discard_stage(slot, &st->stats, 0);
    release_if_empty(slot);
    st->stats.snapshots_rejected_malformed++;
    if (verbose) log_line("dipimetrics: rejected malformed snapshot part for %s/%s", metrics_component_name(hdr.component), hdr.metrics_id);
    return;
  }
  if (slot->stage.len + body_len > STORE_MAX_BLOB_BYTES || blob_append(&slot->stage, body, body_len)) {
    discard_stage(slot, &st->stats, 0);
    release_if_empty(slot);
    st->stats.snapshots_rejected_toolarge++;
    if (verbose) log_line("dipimetrics: dropped oversized snapshot for %s/%s", metrics_component_name(hdr.component), hdr.metrics_id);
    return;
  }
  slot->stage_next = (unsigned)hdr.part + 1;
  slot->activity_mono = now_mono;
  if (hdr.flags & METRICS_FLAG_LAST) commit_stage(slot, &st->stats, &hdr, now_mono);
}

void store_reap_expired(store_t *st, double now_mono, double expiry_s) {
  for (int i = 0; i < STORE_MAX_INSTANCES; i++) {
    store_slot_t *s = &st->slots[i];
    if (s->used && now_mono - (s->valid ? s->received_mono : s->activity_mono) > expiry_s) release_slot(s);
  }
}
