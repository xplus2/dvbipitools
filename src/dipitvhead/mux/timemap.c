/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "pcrclock.h"
#include "timemap.h"

#define K90_MODULUS ((uint64_t)1 << 33)
#define MAX_PCR_STEP_S 60.0
#define SAME_BURST_MAX_STEP_S 0.5

void timemap_init(timemap_t *t) { memset(t, 0, sizeof *t); }

int timemap_pcr_plausible(uint64_t last27, uint64_t cur27, double wall_delta_s) {
  double pcr_delta_s = (double)pcr_sub(cur27, last27) / (double)PCR_CLOCK_HZ;
  if (pcr_delta_s <= 0.0 || pcr_delta_s >= MAX_PCR_STEP_S) return 0;
  if (wall_delta_s <= 0.0) return pcr_delta_s < SAME_BURST_MAX_STEP_S;
  return pcr_delta_s < wall_delta_s * 5.0 + 0.5 && pcr_delta_s > wall_delta_s * 0.2 - 0.1;
}

uint64_t timemap_k90(const timemap_t *t) { return t->k90; }

uint64_t timemap_pcr(timemap_t *t, uint64_t src27, double now_s) {
  uint64_t out;
  if (t->have_pcr && !timemap_pcr_plausible(t->last_src, src27, now_s - t->last_wall)) {
    double wall = now_s - t->last_wall;
    uint64_t wall_ticks = wall > 0.0 ? (uint64_t)(wall * (double)PCR_CLOCK_HZ) % PCR_MODULUS : 0;
    uint64_t want = pcr_sub(pcr_add(t->last_out, wall_ticks), src27);
    t->k90 = ((want + 150) / 300) % K90_MODULUS;
    t->relatches++;
  }
  out = pcr_add(src27, t->k90 * 300ULL);
  t->have_pcr = 1;
  t->last_src = src27;
  t->last_out = out;
  t->last_wall = now_s;
  return out;
}

#define STAMP_MODULUS ((uint64_t)1 << 33)
#define JUMP_MAX90 (2 * 90000)
#define BUCKET90 90000
#define SLEW_MAX90 9
#define GAIN_DIV 8
#define SETPOINT_BUCKETS 2

static int64_t diff33(uint64_t a, uint64_t b) {
  uint64_t d = (a + STAMP_MODULUS - (b % STAMP_MODULUS)) % STAMP_MODULUS;
  return d >= STAMP_MODULUS / 2 ? (int64_t)d - (int64_t)STAMP_MODULUS : (int64_t)d;
}

static void reset_window(timemap_t *t) {
  t->rg_bucket_open = 0;
  t->rg_nset = 0;
}

void timemap_regen_first(timemap_t *t, uint64_t k90) {
  t->k90 = k90 % STAMP_MODULUS;
  t->rg_active = 1;
  t->rg_have_last = 0;
  reset_window(t);
}

int timemap_regen_active(const timemap_t *t) { return t->rg_active; }

static void finish_bucket(timemap_t *t) {
  int64_t err;
  int64_t step;
  if (t->rg_nset < SETPOINT_BUCKETS) {
    t->rg_setpoint = t->rg_nset == 0 || t->rg_bucket_min < t->rg_setpoint ? t->rg_bucket_min : t->rg_setpoint;
    t->rg_nset++;
    return;
  }
  err = t->rg_bucket_min - t->rg_setpoint;
  step = err / GAIN_DIV;
  if (step > SLEW_MAX90) step = SLEW_MAX90;
  if (step < -SLEW_MAX90) step = -SLEW_MAX90;
  t->k90 = (uint64_t)(((int64_t)t->k90 - step + (int64_t)STAMP_MODULUS) % (int64_t)STAMP_MODULUS);
}

int timemap_regen_sample(timemap_t *t, uint64_t stamp90, uint64_t p90, uint64_t lead90) {
  int64_t e;
  if (t->rg_have_last) {
    int64_t delta = diff33(stamp90, t->rg_last_stamp);
    if (delta < 0 || delta > JUMP_MAX90) {
      t->k90 = (stamp90 > p90 + lead90 ? STAMP_MODULUS : 0) + p90 + lead90 - stamp90;
      t->k90 %= STAMP_MODULUS;
      t->rg_last_stamp = stamp90;
      reset_window(t);
      t->rg_relatches++;
      return 1;
    }
  }
  t->rg_last_stamp = stamp90;
  t->rg_have_last = 1;
  e = diff33(stamp90 + t->k90, p90);
  if (!t->rg_bucket_open) {
    t->rg_bucket_open = 1;
    t->rg_bucket_start = p90;
    t->rg_bucket_min = e;
    return 0;
  }
  if (e < t->rg_bucket_min) t->rg_bucket_min = e;
  if (diff33(p90, t->rg_bucket_start) >= BUCKET90) {
    finish_bucket(t);
    t->rg_bucket_start = p90;
    t->rg_bucket_min = diff33(stamp90 + t->k90, p90);
  }
  return 0;
}
