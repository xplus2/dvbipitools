/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TIMEMAP_H
#define DIPITVHEAD_TIMEMAP_H

#include <stdint.h>

typedef struct {
  uint64_t k90;
  int have_pcr;
  uint64_t last_src;
  uint64_t last_out;
  double last_wall;
  unsigned long long relatches;
  int rg_active;
  int rg_have_last;
  uint64_t rg_last_stamp;
  int rg_bucket_open;
  uint64_t rg_bucket_start;
  int64_t rg_bucket_min;
  int rg_nset;
  int64_t rg_setpoint;
  unsigned long long rg_relatches;
} timemap_t;

void timemap_init(timemap_t *t);
int timemap_pcr_plausible(uint64_t last27, uint64_t cur27, double wall_delta_s);
uint64_t timemap_pcr(timemap_t *t, uint64_t src27, double now_s);
uint64_t timemap_k90(const timemap_t *t);

void timemap_regen_first(timemap_t *t, uint64_t k90);
int timemap_regen_active(const timemap_t *t);
int timemap_regen_sample(timemap_t *t, uint64_t stamp90, uint64_t p90, uint64_t lead90);

#endif
