/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DASH_MPD_H
#define DVBIPITOOLS_LIB_DASH_MPD_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

#define DASH_MAX_REPRESENTATIONS 16
#define DASH_MAX_ADAPTATION_SETS 8
#define DASH_MAX_PERIODS 4
#define DASH_MAX_TIMELINE 128

typedef struct {
  unsigned long long t;
  unsigned long long d;
  unsigned long long r;
} dash_timeline_entry_t;

typedef struct {
  char id[64];
  unsigned bandwidth;
  unsigned width;
  unsigned height;
  char init_url[2048];
  char media_url_tmpl[2048];
  unsigned timescale;
  unsigned long long duration;
  unsigned long long start_number;
  unsigned long long availability_time_offset_ms;
  unsigned long long presentation_time_offset;
  dash_timeline_entry_t timeline[DASH_MAX_TIMELINE];
  unsigned n_timeline;
  unsigned long long timeline_skipped;
} dash_representation_t;

typedef struct {
  char mime_type[64];
  dash_representation_t representations[DASH_MAX_REPRESENTATIONS];
  unsigned n_representations;
} dash_adaptation_set_t;

typedef struct {
  dash_adaptation_set_t adaptation_sets[DASH_MAX_ADAPTATION_SETS];
  unsigned n_adaptation_sets;
  unsigned long long start_ms;
} dash_period_t;

typedef struct {
  unsigned n_periods;
  int is_low_latency;
  unsigned latency_target_ms;
  unsigned latency_min_ms;
  unsigned latency_max_ms;
  double playback_rate_min;
  double playback_rate_max;
  int is_dynamic;
  unsigned minimum_update_period_ms;
  int has_availability_start;
  unsigned long long availability_start_ms;
  unsigned long long time_shift_buffer_ms;
  dash_period_t periods[DASH_MAX_PERIODS]; /* last: dash_mpd_parse() leaves it alone, bounded by n_periods */
} dash_mpd_t;

int dash_mpd_parse(const char *body, const http_url_t *base, dash_mpd_t *out);

int dash_pick_highest(const dash_adaptation_set_t *as);

unsigned long long dash_wall_ms(void);

/* period holding the live edge: last period if dynamic without availabilityStartTime, else by wall clock. 0 if static */
unsigned dash_mpd_live_period(const dash_mpd_t *m);

/* expands $RepresentationID$ $Bandwidth$ $Number$ $Time$ $$, optional %0Nd after the name */
int dash_media_url(const dash_representation_t *r, unsigned long long number, unsigned long long time, char *out, size_t n);

int dash_init_url(const dash_representation_t *r, char *out, size_t n);

/* 0 if unknown. SegmentTemplate duration, else last SegmentTimeline entry */
unsigned long long dash_segment_duration_ms(const dash_representation_t *r);

/* newest complete segment index (0 = startNumber) when elapsed_ms have passed since period start. 0 none yet, 1 *idx set. duration addressing only */
int dash_edge_index(const dash_representation_t *r, unsigned long long elapsed_ms, unsigned long long *idx);

/* ms after period start at which segment idx may be requested */
unsigned long long dash_index_available_ms(const dash_representation_t *r, unsigned long long idx);

/* $Time$ value of duration-addressed segment idx */
unsigned long long dash_index_time(const dash_representation_t *r, unsigned long long idx);

/* newest timeline segment. 0 if no timeline, else 1 with *k = its position */
int dash_timeline_last(const dash_representation_t *r, unsigned long long *t, unsigned long long *d, unsigned long long *k);

/* segment starting exactly at time. 0 if none, else 1 with *k = position (for $Number$) and *d */
int dash_timeline_find(const dash_representation_t *r, unsigned long long time, unsigned long long *d, unsigned long long *k);

/* start of oldest timeline segment, 0 if no timeline */
unsigned long long dash_timeline_first(const dash_representation_t *r);

unsigned long long dash_effective_availability_ms(unsigned long long nominal_availability_ms, unsigned long long availability_time_offset_ms);

#endif
