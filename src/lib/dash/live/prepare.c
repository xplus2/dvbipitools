/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/dash/live.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/dash/mpd.h"
#include "lib/dash/live/live_int.h"

static int timeline_ended(const dash_live_t *h) {
  const dash_timeline_entry_t *e = &h->repr.timeline[h->repr.n_timeline - 1];
  return last_period(h) && h->seg_time >= e->t + e->d * (e->r + 1);
}

static int duration_ended(const dash_live_t *h) {
  const dash_period_t *p = &h->mpd.periods[h->period_idx];
  unsigned long long total = p->duration_ms;
  unsigned long long seg_ms = dash_segment_duration_ms(&h->repr);
  if (!total && h->mpd.media_presentation_duration_ms > p->start_ms) total = h->mpd.media_presentation_duration_ms - p->start_ms;
  if (!total || !seg_ms || !last_period(h)) return 0;
  return h->seg_idx >= (total + seg_ms - 1) / seg_ms;
}

static prep_t prepare_timeline(dash_live_t *h, char *url, size_t n) {
  unsigned long long d;
  unsigned long long k;
  double now = mono_seconds();
  if (!dash_timeline_find(&h->repr, h->seg_time, &d, &k)) {
    if (h->mpd.is_dynamic && h->seg_time < dash_timeline_first(&h->repr)) {
      log_throttled(&h->lag_throttle, LOG_THROTTLE_WINDOW_S, "LW400236", "input %u (%s): DASH fell behind live edge, rejoining", h->idx, h->label ? h->label : "?");
      join_position(h);
    }
    if (!dash_timeline_find(&h->repr, h->seg_time, &d, &k)) {
      double retry = segment_duration_s(h) / 2;
      if (!h->mpd.is_dynamic) {
        if (timeline_ended(h)) h->finished = 1;
        h->next_segment_at = now + DASH_STATIC_RETRY_S;
        return PREP_WAIT;
      }
      if (retry < 0.25) retry = 0.25;
      if (retry > h->update_period_s && h->update_period_s > 0.0) retry = h->update_period_s;
      h->next_mpd_poll_at = h->mpd_fetched_at + retry > now ? h->mpd_fetched_at + retry : now;
      h->next_segment_at = h->next_mpd_poll_at;
      h->wait_timeline = 1;
      return PREP_NEED_MPD;
    }
  }
  h->seg_d = d;
  dash_media_url(&h->repr, h->repr.start_number + k, h->seg_time, url, n);
  return PREP_READY;
}

static prep_t prepare_duration(dash_live_t *h, char *url, size_t n) {
  if (!h->mpd.is_dynamic && duration_ended(h)) {
    h->finished = 1;
    return PREP_WAIT;
  }
  if (wall_anchored(h)) {
    unsigned long long el = elapsed_ms(h);
    unsigned long long edge;
    unsigned long long avail;
    unsigned long long dur_ms = dash_segment_duration_ms(&h->repr);
    unsigned long long lag = (h->mpd.time_shift_buffer_ms ? h->mpd.time_shift_buffer_ms : DASH_MAX_LAG_MS) / (dur_ms ? dur_ms : 1);
    if (lag < 2) lag = 2;
    if (dash_edge_index(&h->repr, el, &edge) && h->seg_idx + lag < edge) {
      log_throttled(&h->lag_throttle, LOG_THROTTLE_WINDOW_S, "LW400236", "input %u (%s): DASH fell behind live edge, rejoining", h->idx, h->label ? h->label : "?");
      h->seg_idx = edge;
    }
    avail = dash_index_available_ms(&h->repr, h->seg_idx);
    if (avail > el) {
      h->next_segment_at = mono_seconds() + (double)(avail - el) / 1000.0;
      return PREP_WAIT;
    }
  }
  dash_media_url(&h->repr, h->repr.start_number + h->seg_idx, dash_index_time(&h->repr, h->seg_idx), url, n);
  return PREP_READY;
}

prep_t prepare_segment(dash_live_t *h, char *url, size_t n) {
  switch_period_if_due(h);
  return uses_timeline(h) ? prepare_timeline(h, url, n) : prepare_duration(h, url, n);
}

void advance_segment(dash_live_t *h) {
  double dur = segment_duration_s(h);
  double now = mono_seconds();
  if (uses_timeline(h)) h->seg_time += h->seg_d;
  else h->seg_idx++;
  if (!paced(h)) {
    h->next_segment_at = now;
    return;
  }
  h->next_segment_at += dur;
  if (h->next_segment_at + 10 * dur < now) h->next_segment_at = now;
}
