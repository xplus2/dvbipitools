/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../fcc/burst.h"
#include "../version.h"
#include "run.h"

typedef struct {
  size_t idx;
  int fd;
  struct sockaddr_storage addr;
  socklen_t addrlen;
  burst_t *b;
} pacer_snap_t;

/* seqlock reader for one slot, bounded retry: repeated overlap = treated as unreadable this tick */
static int burst_slot_read(const burst_slot_t *slot, int *in_use, uint64_t *words, socklen_t *addrlen, int *fd, burst_t **b) {
  unsigned g;
  SEQLOCK_READ_LOOP(&slot->gen, g) {
    *in_use = atomic_load_explicit(&slot->in_use, memory_order_relaxed);
    if (*in_use) {
      for (size_t w = 0; w < BURST_ADDR_WORDS; w++)
        words[w] = atomic_load_explicit(&slot->addr_words[w], memory_order_relaxed);
      *addrlen = atomic_load_explicit(&slot->addrlen, memory_order_relaxed);
      *fd = atomic_load_explicit(&slot->fd, memory_order_relaxed);
      *b = atomic_load_explicit(&slot->b, memory_order_relaxed);
    }
    if (SEQLOCK_READ_OK(&slot->gen, g))
      return 1;
  }
  return 0; /* repeated race, skip this tick */
}

/* scan is lock-free: seqlock-guarded read per slot, retried on writer overlap, skipped on
   repeated overlap (next tick picks it up). done-cleanup below takes table lock */
void *pacer_main(void *arg) {
  pacer_ctx_t *pc = (pacer_ctx_t *)arg;
  struct timespec tick = {0, 20 * 1000 * 1000}; /* 20ms */
  pacer_snap_t *snap = calloc(pc->bursts->cap, sizeof *snap);

  if (!snap) {
    log_line(TOOL_NAME ": pacer: out of memory, burst pacing disabled");
    return NULL;
  }

  while (!signal_stop_requested()) {
    size_t n = 0;
    size_t scan_upto = atomic_load_explicit(&pc->bursts->high_water_mark, memory_order_relaxed);

    nanosleep(&tick, NULL);

    for (size_t i = 0; i < scan_upto; i++) {
      const burst_slot_t *slot = &pc->bursts->slots[i];
      int in_use = 0;
      uint64_t words[BURST_ADDR_WORDS] = {0};
      int fd = 0;
      burst_t *b = NULL;
      socklen_t addrlen = 0;

      if (!burst_slot_read(slot, &in_use, words, &addrlen, &fd, &b) || !in_use)
        continue;
      burst_acquire(b);
      snap[n].idx = i;
      snap[n].fd = fd;
      memcpy(&snap[n].addr, words, sizeof snap[n].addr);
      snap[n].addrlen = addrlen;
      snap[n].b = b;
      n++;
    }

    for (size_t i = 0; i < n; i++) {
      unicast_dest_t dst;
      burst_tick_result_t r;
      int owns_slot = 0;
      int remove_slot = 0;
      double bytes_before = snap[i].b->bytes_sent; /* single-ticker-thread field, burst.h */

      dst.fd = snap[i].fd;
      dst.to = (const struct sockaddr *)&snap[i].addr;
      dst.tolen = snap[i].addrlen;
      dst.congestion = 0;
      r = burst_tick(snap[i].b, pc->duration_cap_ms, burst_send_cb, &dst);
      burst_table_note_bytes_sent(pc->bursts, (uint64_t)(snap[i].b->bytes_sent - bytes_before));
      owns_slot = burst_table_release(pc->bursts, snap[i].idx, snap[i].b, dst.congestion || r == BURST_TICK_DONE, &remove_slot);

      if (owns_slot && dst.congestion)
        send_rams_i(&dst, 0, 0, (uint16_t)BURST_CONGESTION, NULL);
      else if (owns_slot && r == BURST_TICK_DONE)
        send_rams_i(&dst, 0, 0, (uint16_t)BURST_DONE, NULL);

      if (remove_slot)
        burst_release(snap[i].b); /* drop slot ownership */
      burst_release(snap[i].b); /* drop pacer snapshot */
    }
  }
  free(snap);
  return NULL;
}
