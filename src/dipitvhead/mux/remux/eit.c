/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/tspack.h"
#include "lib/mux/tspacket_write.h"

#include "priv.h"

/* key: table_id+section_number. updates if present, appends if room, else drops */
static void eit_queue_put(remux_t *r, unsigned char table_id, unsigned char section_number, const unsigned char *data, size_t len, ts_metrics_t *tsm) {
  for (int i = 0; i < r->eit_queue_count; i++) {
    if (r->eit_queue[i].table_id == table_id && r->eit_queue[i].section_number == section_number) {
      memcpy(r->eit_queue[i].data, data, len);
      r->eit_queue[i].len = len;
      return;
    }
  }
  if (r->eit_queue_count >= EIT_QUEUE_CAP) {
    if (tsm)
      tsm->eit_queue_drops_total++;
    return;
  }
  r->eit_queue[r->eit_queue_count].table_id = table_id;
  r->eit_queue[r->eit_queue_count].section_number = section_number;
  memcpy(r->eit_queue[r->eit_queue_count].data, data, len);
  r->eit_queue[r->eit_queue_count].len = len;
  r->eit_queue_count++;
}

void capture_eit_section(remux_t *r, const unsigned char *pkt188, ts_metrics_t *tsm) {
  const unsigned char *pl;
  size_t plen;
  int pusi;
  const unsigned char *sec;
  unsigned service_id;

  if (!tspack_payload(pkt188, &pl, &plen, &pusi))
    return;
  if (!psi_section_asm_feed(&r->eit_asm, pl, plen, pusi))
    return;
  sec = r->eit_asm.buf;
  if (r->eit_asm.len < 8 || r->eit_asm.len > sizeof r->eit_queue[0].data)
    return;
  service_id = ((unsigned)sec[3] << 8) | sec[4];
  if (service_id != r->src_service_id)
    return;
  eit_queue_put(r, sec[0], sec[6], sec, r->eit_asm.len, tsm);
}

size_t remux_emit_eit(remux_t *r, unsigned pid, unsigned char *cc, size_t max_packets, remux_packet_cb cb, void *ctx) {
  unsigned char ptr0 = 0x00;
  size_t n;

  if (r->eit_queue_count == 0)
    return 0;
  n = ts_packet_emit_partial(pid, cc, &ptr0, r->eit_queue[0].data, r->eit_queue[0].len, &r->eit_drain_off, max_packets, cb, ctx);
  if (r->eit_drain_off >= r->eit_queue[0].len) {
    r->eit_queue_count--;
    memmove(&r->eit_queue[0], &r->eit_queue[1], (size_t)r->eit_queue_count * sizeof r->eit_queue[0]);
    r->eit_drain_off = 0;
  }
  return n;
}

int remux_eit_pending(const remux_t *r) { return r->eit_queue_count > 0; }

/* nonzero: partial section pending, caller must stay on r till done */
int remux_eit_mid_section(const remux_t *r) { return r->eit_drain_off > 0; }
