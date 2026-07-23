/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "bitrate.h"

#define PACKET_BITS (188.0 * 8.0)

struct bitrate_pacer {
  double target_bps;
  int stuff, burst_limit;
  double start;
  unsigned long long bits_sent;
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

bitrate_pacer_t *bitrate_pacer_new(double target_bps, int stuff, int burst_limit) {
  bitrate_pacer_t *p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->target_bps = target_bps;
  p->stuff = stuff;
  p->burst_limit = burst_limit;
  p->start = mono();
  return p;
}

void bitrate_pacer_free(bitrate_pacer_t *p) { free(p); }

void bitrate_pace(bitrate_pacer_t *p) {
  double ahead_s;
  if (!p->burst_limit || p->target_bps <= 0.0)
    return;
  ahead_s = ((double)p->bits_sent - (mono() - p->start) * p->target_bps) / p->target_bps;
  if (ahead_s > 0.0)
    usleep((useconds_t)(ahead_s * 1e6));
}

void bitrate_account(bitrate_pacer_t *p) { p->bits_sent += (unsigned long long)PACKET_BITS; }

int bitrate_stuff_due(bitrate_pacer_t *p) {
  double behind_bits;
  if (!p->stuff || p->target_bps <= 0.0)
    return 0;
  behind_bits = (mono() - p->start) * p->target_bps - (double)p->bits_sent;
  if (behind_bits <= 0.0)
    return 0;
  return (int)(behind_bits / PACKET_BITS);
}
