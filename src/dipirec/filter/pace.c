/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/signal.h"

#include "pace.h"

#define RESYNC_THRESHOLD_S 2.0 /* real discontinuity, not scheduler jitter */

struct pace_ctrl {
  int have_baseline;
  uint64_t last_raw;
  uint64_t elapsed_ticks; /* since baseline, wrap-accumulated */
  double first_wall;
};

pace_ctrl_t *pace_new(void) { return calloc(1, sizeof(pace_ctrl_t)); }
void pace_free(pace_ctrl_t *p) { free(p); }
int pace_has_baseline(const pace_ctrl_t *p) { return p->have_baseline; }

void pace_accumulate(uint64_t *last_raw, uint64_t *elapsed_ticks, uint64_t raw, unsigned bits) {
  uint64_t modulus = (uint64_t)1 << bits;

  if (raw >= *last_raw)
    *elapsed_ticks += raw - *last_raw;
  else if (*last_raw - raw > modulus / 2)
    *elapsed_ticks += modulus - *last_raw + raw; /* wrapped */
  /* else: raw slightly behind last_raw, jitter/reorder, ignore */
  *last_raw = raw;
}

/* raw: sample, bits: its width, hz: tick rate. baselines on first call,
   wrap-accumulates, sleeps to keep wall clock in step, resyncs on real jump instead of catching up */
static void tick_step(pace_ctrl_t *p, uint64_t raw, unsigned bits, double hz) {
  double now, target;
  if (!p->have_baseline) {
    p->have_baseline = 1;
    p->last_raw = raw;
    p->elapsed_ticks = 0;
    p->first_wall = mono_seconds();
    return;
  }

  pace_accumulate(&p->last_raw, &p->elapsed_ticks, raw, bits);

  now = mono_seconds();
  target = p->first_wall + (double)p->elapsed_ticks / hz;
  if (now < target) {
    sleep_interruptible(target - now);
  } else if (now - target > RESYNC_THRESHOLD_S) {
    p->first_wall = now;
    p->elapsed_ticks = 0;
  }
}

void pace_feed_rtp_ts(pace_ctrl_t *p, uint32_t rtp_ts_90k) {
  tick_step(p, rtp_ts_90k, 32, 90000.0);
}

void pace_feed_pcr_pkt(pace_ctrl_t *p, const unsigned char *pkt188, unsigned pcr_pid) {
  unsigned pid;
  uint64_t base;

  if (!pcr_pid || pkt188[0] != 0x47)
    return;
  pid = (((unsigned)pkt188[1] & 0x1F) << 8) | pkt188[2];
  if (pid != pcr_pid || !(pkt188[3] & 0x20))
    return;
  if (pkt188[4] < 7 || !(pkt188[5] & 0x10)) /* too short for PCR, or no PCR_flag */
    return;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) |
         ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  tick_step(p, base, 33, 90000.0);
}
