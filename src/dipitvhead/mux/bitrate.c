/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "bitrate.h"

#define PACKET_BITS (188.0 * 8.0)
#define OVERAGE_THRESHOLD_S 2.0     /* sustained backlog before it's a real misconfiguration, not a burst */
#define OVERAGE_LOG_COOLDOWN_S 30.0 /* don't spam if it stays misconfigured */

struct bitrate_pacer {
  double target_bps;
  int stuff, burst_limit;
  double start;
  unsigned long long bits_sent;
  double last_overage_log; /* < 0 = never logged */
};

bitrate_pacer_t *bitrate_pacer_new(double target_bps, int stuff, int burst_limit) {
  bitrate_pacer_t *p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->target_bps = target_bps;
  p->stuff = stuff;
  p->burst_limit = burst_limit;
  p->start = mono_seconds();
  p->last_overage_log = -1.0;
  return p;
}

void bitrate_pacer_free(bitrate_pacer_t *p) { free(p); }

void bitrate_pace(bitrate_pacer_t *p) {
  double ahead_s;
  if (!p || !p->burst_limit || p->target_bps <= 0.0)
    return;
  ahead_s = ((double)p->bits_sent - (mono_seconds() - p->start) * p->target_bps) / p->target_bps;
  if (ahead_s > 0.0) {
    struct timespec ts = {(time_t)ahead_s, (long)((ahead_s - (time_t)ahead_s) * 1e9)};
    nanosleep(&ts, NULL);
  }
}

void bitrate_account_n(bitrate_pacer_t *p, unsigned n) {
  double now, ahead_s;

  if (!p)
    return;
  p->bits_sent += (unsigned long long)PACKET_BITS * n;
  if (p->target_bps <= 0.0)
    return;
  now = mono_seconds();
  ahead_s = ((double)p->bits_sent - (now - p->start) * p->target_bps) / p->target_bps;
  if (ahead_s > OVERAGE_THRESHOLD_S && (p->last_overage_log < 0.0 || now - p->last_overage_log >= OVERAGE_LOG_COOLDOWN_S)) {
    log_line("bitrate: content exceeds -b target by >%.0fs of backlog, not corrected (pass -B to throttle)", OVERAGE_THRESHOLD_S);
    p->last_overage_log = now;
  }
}

void bitrate_account(bitrate_pacer_t *p) { bitrate_account_n(p, 1); }

int bitrate_stuff_due(bitrate_pacer_t *p) {
  double behind_bits;
  if (!p->stuff || p->target_bps <= 0.0)
    return 0;
  behind_bits = (mono_seconds() - p->start) * p->target_bps - (double)p->bits_sent;
  if (behind_bits <= 0.0)
    return 0;
  return (int)(behind_bits / PACKET_BITS);
}
