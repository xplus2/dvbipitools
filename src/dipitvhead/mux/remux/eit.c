/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/demux/tspack.h"
#include "lib/helper/beutil.h"
#include "lib/mux/tspacket_write.h"

#include "priv.h"

/* actual TS only: 0x4E present/following, 0x50-0x5F schedule */
static int eit_table_id_actual(unsigned char table_id) { return table_id == 0x4E || (table_id >= 0x50 && table_id <= 0x5F); }

static eit_section_t *eit_at(remux_t *r, int i) { return &r->eit_queue[r->eit_order[i]]; }

/* drop queue position i, its slot goes to free tail. sections never move */
static void eit_remove(remux_t *r, int i) {
  unsigned char slot = r->eit_order[i];
  r->eit_queue_count--;
  memmove(&r->eit_order[i], &r->eit_order[i + 1], (size_t)(EIT_QUEUE_CAP - 1 - i));
  r->eit_order[EIT_QUEUE_CAP - 1] = slot;
}

/* newest schedule entry not mid-send, -1 if none */
static int eit_evict_candidate(remux_t *r) {
  for (int i = r->eit_queue_count - 1; i >= (r->eit_drain_off > 0 ? 1 : 0); i--)
    if (eit_at(r, i)->table_id != 0x4E) return i;
  return -1;
}

/* queued sections of table_id with another version are stale, never emit mixed. head midsend kept */
static void eit_drop_stale(remux_t *r, unsigned char table_id, unsigned char ver) {
  for (int i = r->eit_queue_count - 1; i >= (r->eit_drain_off > 0 ? 1 : 0); i--) {
    const eit_section_t *q = eit_at(r, i);
    if (q->table_id == table_id && ((q->data[5] >> 1) & 0x1F) != ver) eit_remove(r, i);
  }
}

/* key: table_id+section_number. updates if present, appends if room, else drops. head mid-send never updated. full: p/f evicts newest schedule */
static void eit_queue_put(remux_t *r, unsigned char table_id, unsigned char section_number, const unsigned char *data, size_t len, ts_metrics_t *tsm) {
  eit_section_t *slot;
  eit_drop_stale(r, table_id, (data[5] >> 1) & 0x1F);
  for (int i = (r->eit_drain_off > 0) ? 1 : 0; i < r->eit_queue_count; i++) {
    eit_section_t *q = eit_at(r, i);
    if (q->table_id == table_id && q->section_number == section_number) {
      memcpy(q->data, data, len);
      q->len = len;
      return;
    }
  }
  if (r->eit_queue_count >= EIT_QUEUE_CAP) {
    int victim = (table_id == 0x4E) ? eit_evict_candidate(r) : -1;
    if (victim < 0) {
      if (tsm) tsm->eit_queue_drops_total++;
      return;
    }
    eit_remove(r, victim);
    if (tsm) tsm->eit_queue_drops_total++;
  }
  slot = eit_at(r, r->eit_queue_count);
  slot->table_id = table_id;
  slot->section_number = section_number;
  memcpy(slot->data, data, len);
  slot->len = len;
  r->eit_queue_count++;
}

static void eit_section_done(remux_t *r, ts_metrics_t *tsm) {
  const unsigned char *sec = r->eit_asm.buf;
  unsigned service_id;
  unsigned char out[sizeof r->eit_queue[0].data];

  if (r->eit_asm.len < 16 || r->eit_asm.len > sizeof r->eit_queue[0].data) return;
  if (crc32_mpeg(sec, r->eit_asm.len) != 0) return;
  if (!eit_table_id_actual(sec[0])) return;
  service_id = ((unsigned)sec[3] << 8) | sec[4];
  if (service_id != r->src_service_id) return;
  memcpy(out, sec, r->eit_asm.len);
  out[3] = (unsigned char)(r->input.sid >> 8);
  out[4] = (unsigned char)r->input.sid;
  out[8] = (unsigned char)(r->cfg->tsid >> 8);
  out[9] = (unsigned char)r->cfg->tsid;
  out[10] = (unsigned char)(r->cfg->onid >> 8);
  out[11] = (unsigned char)r->cfg->onid;
  be32_put(out + r->eit_asm.len - 4, crc32_mpeg(out, r->eit_asm.len - 4));
  eit_queue_put(r, out[0], out[6], out, r->eit_asm.len, tsm);
}

void capture_eit_section(remux_t *r, const unsigned char *pkt188, ts_metrics_t *tsm) {
  const unsigned char *pl;
  size_t plen;
  int pusi;
  if (!tspack_payload(pkt188, &pl, &plen, &pusi)) return;
  if (!psi_section_asm_cc(&r->eit_asm, pkt188)) return;
  for (int got = psi_section_asm_feed(&r->eit_asm, pl, plen, pusi); got; got = psi_section_asm_next(&r->eit_asm, pl, plen))
    eit_section_done(r, tsm);
}

size_t remux_emit_eit(remux_t *r, unsigned pid, unsigned char *cc, size_t max_packets, remux_packet_cb cb, void *ctx) {
  unsigned char ptr0 = 0x00;
  const eit_section_t *head;
  size_t n;

  if (r->eit_queue_count == 0) return 0;
  head = eit_at(r, 0);
  n = ts_packet_emit_partial(pid, cc, &ptr0, head->data, head->len, &r->eit_drain_off, max_packets, cb, ctx);
  if (r->eit_drain_off >= head->len) {
    eit_remove(r, 0);
    r->eit_drain_off = 0;
  }
  return n;
}

int remux_eit_pending(const remux_t *r) { return r->eit_queue_count > 0; }

/* nonzero: partial section pending, caller must stay on r till done */
int remux_eit_mid_section(const remux_t *r) { return r->eit_drain_off > 0; }
