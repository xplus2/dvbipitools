/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DASH_MPD_H
#define DVBIPITOOLS_LIB_DASH_MPD_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

#define DASH_MAX_REPRESENTATIONS 16
#define DASH_MAX_ADAPTATION_SETS 8
#define DASH_MAX_PERIODS 4

typedef struct {
  char id[64];
  unsigned bandwidth;
  unsigned width, height;
  char init_url[2048];
  char media_url_tmpl[2048];
  unsigned timescale;
  unsigned long long duration;
  unsigned long long start_number;
  unsigned long long availability_time_offset_ms;
} dash_representation_t;

typedef struct {
  char mime_type[64];
  dash_representation_t representations[DASH_MAX_REPRESENTATIONS];
  unsigned n_representations;
} dash_adaptation_set_t;

typedef struct {
  dash_adaptation_set_t adaptation_sets[DASH_MAX_ADAPTATION_SETS];
  unsigned n_adaptation_sets;
} dash_period_t;

typedef struct {
  dash_period_t periods[DASH_MAX_PERIODS];
  unsigned n_periods;
  int is_low_latency;
  unsigned latency_target_ms, latency_min_ms, latency_max_ms;
  double playback_rate_min, playback_rate_max;
  int is_dynamic;
  unsigned minimum_update_period_ms;
} dash_mpd_t;

int dash_mpd_parse(char *body, const http_url_t *base, dash_mpd_t *out);

int dash_pick_highest(const dash_adaptation_set_t *as);

int dash_media_url(const dash_representation_t *r, unsigned long long number, unsigned long long time, char *out, size_t n);

unsigned long long dash_effective_availability_ms(unsigned long long nominal_availability_ms, unsigned long long availability_time_offset_ms);

#endif
