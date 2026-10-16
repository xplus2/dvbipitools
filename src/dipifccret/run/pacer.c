/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "../fcc/burst.h"
#include "../version.h"
#include "run.h"

typedef struct {
  size_t idx;
  burst_snap_t s;
} pacer_snap_t;

/* scan peeks in_use lock-free, snapshot + burst ref taken under stripe lock */
void *pacer_main(void *arg) {
  pacer_ctx_t *pc = (pacer_ctx_t *)arg;
  struct timespec deadline;
  pacer_snap_t *snap;

  if (cpuaff_pin(pc->cpuaff, pc->cpu_idx, "burst pacer") < 0)
    log_code("FW500245", TOOL_NAME ": burst pacer: cpu pin failed, running unpinned");
  snap = calloc(pc->bursts->cap, sizeof *snap);
  if (!snap) {
    log_code("FW500180", TOOL_NAME ": pacer: out of memory, burst pacing disabled");
    return NULL;
  }

  clock_gettime(CLOCK_MONOTONIC, &deadline);

  while (!signal_stop_requested()) {
    size_t n = 0;
    size_t scan_upto = atomic_load_explicit(&pc->bursts->high_water_mark, memory_order_relaxed);

    deadline.tv_nsec += 20 * 1000 * 1000;
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_nsec -= 1000000000L;
      deadline.tv_sec += 1;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    for (size_t i = 0; i < scan_upto; i++) {
      const burst_slot_t *slot = &pc->bursts->slots[i];
      if (!atomic_load_explicit(&slot->in_use, memory_order_relaxed)) continue;
      if (!burst_table_acquire(pc->bursts, i, &snap[n].s)) continue;
      snap[n].idx = i;
      n++;
    }

    for (size_t i = 0; i < n; i++) {
      unicast_dest_t dst;
      burst_tick_result_t r;
      int owns_slot = 0;
      int remove_slot = 0;
      double bytes_before = snap[i].s.b->bytes_sent; /* single-ticker-thread field, burst.h */

      dst.fd = snap[i].s.fd;
      dst.to = (const struct sockaddr *)&snap[i].s.addr;
      dst.tolen = snap[i].s.addrlen;
      dst.congestion = 0;
      r = burst_tick(snap[i].s.b, pc->duration_cap_ms, burst_send_cb, &dst);
      burst_table_note_bytes_sent(pc->bursts, (uint64_t)(snap[i].s.b->bytes_sent - bytes_before));
      owns_slot = burst_table_release(pc->bursts, snap[i].idx, snap[i].s.b, dst.congestion || r == BURST_TICK_DONE, &remove_slot);
      if (owns_slot && (dst.congestion || r == BURST_TICK_DONE)) {
        uint32_t ssrc = atomic_load_explicit(&snap[i].s.b->channel->ssrc, memory_order_relaxed);
        send_rams_i(&dst, ssrc, ssrc, (uint16_t)(dst.congestion ? BURST_CONGESTION : BURST_DONE), NULL);
      }

      if (remove_slot) burst_release(snap[i].s.b); /* drop slot ownership */
      burst_release(snap[i].s.b); /* drop pacer snapshot */
    }
  }
  free(snap);
  return NULL;
}
