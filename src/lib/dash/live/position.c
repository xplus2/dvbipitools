/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/dash/live.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"
#include "lib/dash/mpd.h"
#include "lib/dash/live/live_int.h"

int uses_timeline(const dash_live_t *h) { return h->repr.n_timeline > 0; }

int wall_anchored(const dash_live_t *h) { return h->mpd.is_dynamic && h->mpd.has_availability_start && !uses_timeline(h) && h->repr.duration; }

int paced(const dash_live_t *h) { return !wall_anchored(h) && !(uses_timeline(h) && h->mpd.is_dynamic); }

static double timescale_s(const dash_representation_t *r, unsigned long long ticks) { return (double)ticks / (double)(r->timescale ? r->timescale : 1); }

double segment_duration_s(const dash_live_t *h) {
  if (uses_timeline(h) && h->seg_d) return timescale_s(&h->repr, h->seg_d);
  if (h->repr.duration) return timescale_s(&h->repr, h->repr.duration);
  return DASH_DEFAULT_SEGMENT_DURATION_S;
}

unsigned long long elapsed_ms(const dash_live_t *h) {
  unsigned long long since = h->mpd.availability_start_ms + h->cur_period_start_ms;
  unsigned long long now = dash_wall_ms();
  return now > since ? now - since : 0;
}

static unsigned period_for_ms(const dash_mpd_t *m, unsigned long long ms) {
  unsigned p = 0;
  for (unsigned i = 1; i < m->n_periods; i++)
    if (m->periods[i].start_ms <= ms) p = i;
  return p;
}

int find_period(const dash_live_t *h, unsigned *out) {
  for (unsigned i = 0; i < h->mpd.n_periods; i++) {
    if (h->mpd.periods[i].start_ms == h->cur_period_start_ms) {
      *out = i;
      return 1;
    }
  }
  return 0;
}

int pick_representation(dash_live_t *h, unsigned period) {
  const dash_period_t *p = &h->mpd.periods[period];
  const dash_representation_t *r;
  unsigned ai = h->adaptation_set_index;
  char old_init[2048] = "";
  char new_init[2048];
  int best;
  if (h->as_mime[0] && (ai >= p->n_adaptation_sets || strcmp(p->adaptation_sets[ai].mime_type, h->as_mime))) {
    for (unsigned i = 0; i < p->n_adaptation_sets; i++) {
      if (!strcmp(p->adaptation_sets[i].mime_type, h->as_mime)) {
        ai = i;
        break;
      }
    }
  }
  if (ai >= p->n_adaptation_sets) return 0;
  best = dash_pick_highest(&p->adaptation_sets[ai]);
  if (best < 0) return 0;
  if (!h->as_mime[0]) bufcpy(h->as_mime, sizeof h->as_mime, p->adaptation_sets[ai].mime_type);
  r = &p->adaptation_sets[ai].representations[(unsigned)best];
  dash_init_url(r, new_init, sizeof new_init);
  if (h->have_representation) dash_init_url(&h->repr, old_init, sizeof old_init);
  if (!h->have_representation || strcmp(old_init, new_init)) h->init_fetched = 0;
  h->repr = *r;
  h->have_representation = 1;
  h->period_idx = period;
  h->cur_period_start_ms = p->start_ms;
  return 1;
}

void join_position(dash_live_t *h) {
  unsigned long long t;
  unsigned long long d;
  unsigned long long k;
  h->positioned = 1;
  h->fail_count = 0;
  if (uses_timeline(h)) {
    if (h->mpd.is_dynamic && dash_timeline_last(&h->repr, &t, &d, &k)) h->seg_time = t;
    else h->seg_time = dash_timeline_first(&h->repr);
  } else if (wall_anchored(h)) {
    if (!dash_edge_index(&h->repr, elapsed_ms(h), &h->seg_idx)) h->seg_idx = 0;
  } else {
    h->seg_idx = 0;
  }
}

static unsigned long long position_ms(const dash_live_t *h) {
  unsigned long long ts = h->repr.timescale ? h->repr.timescale : 1;
  if (uses_timeline(h)) return h->cur_period_start_ms + (h->seg_time > h->repr.presentation_time_offset ? h->seg_time - h->repr.presentation_time_offset : 0) * 1000ULL / ts;
  return h->cur_period_start_ms + h->seg_idx * h->repr.duration * 1000ULL / ts;
}

void switch_period_if_due(dash_live_t *h) {
  unsigned long long pos;
  unsigned p;
  if (h->mpd.n_periods < 2) return;
  pos = position_ms(h);
  p = period_for_ms(&h->mpd, pos);
  if (p == h->period_idx || h->mpd.periods[p].start_ms <= h->cur_period_start_ms) return;
  if (!pick_representation(h, p)) return;
  if (uses_timeline(h)) h->seg_time = dash_timeline_first(&h->repr);
  else if (wall_anchored(h) && dash_segment_duration_ms(&h->repr)) h->seg_idx = (pos - h->cur_period_start_ms) / dash_segment_duration_ms(&h->repr);
  else h->seg_idx = 0;
  h->next_segment_at = mono_seconds();
}

int last_period(const dash_live_t *h) { return h->period_idx + 1 >= h->mpd.n_periods; }
