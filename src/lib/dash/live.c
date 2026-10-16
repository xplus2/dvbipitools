/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "live.h"

#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "mpd.h"

#define DASH_ETAG_MAX 128
#define DASH_MPD_BUF_CAP (64 * 1024)
#define DASH_SEG_BUF_CAP (2 * 1024 * 1024)
#define DASH_OUT_BUF_CAP (2 * 1024 * 1024)
#define DASH_OUT_BUF_PREFETCH_HEADROOM (256 * 1024)
#define DASH_DEFAULT_UPDATE_PERIOD_S 6
#define DASH_DEFAULT_SEGMENT_DURATION_S 6
#define DASH_MAX_REUSE_AGE_S 15
#define DASH_MAX_LAG_MS 30000
#define DASH_MAX_FAILS 5
#define DASH_SEG_RETRY_S 0.5
#define DASH_STATIC_RETRY_S 2.0
#define DASH_INIT_RETRY_S 1.0

typedef enum { DASH_LIVE_IDLE, DASH_LIVE_FETCHING_MPD, DASH_LIVE_FETCHING_INIT, DASH_LIVE_FETCHING_SEGMENT } dash_live_phase_t;

struct dash_live {
  http_url_t mpd_url;
  char user_agent[128];
  int insecure;
  unsigned idx;
  const char *label;
  unsigned adaptation_set_index;
  dash_insp_t si;
  dash_segment_cb cb;
  void *cb_ctx;

  dash_live_phase_t phase;
  http_fetch_t *fetch;
  http_t *reuse;
  double reuse_established_at;
  double next_mpd_poll_at;
  double next_segment_at;
  char etag[DASH_ETAG_MAX];

  int have_representation;
  dash_mpd_t mpd;
  dash_representation_t repr;
  unsigned period_idx;
  unsigned long long cur_period_start_ms;
  char as_mime[64];
  int init_fetched;
  int positioned;
  unsigned long long seg_idx;
  unsigned long long seg_time;
  unsigned long long seg_d;
  int wait_timeline;
  unsigned fail_count;
  double mpd_fetched_at;
  double update_period_s;
  log_throttle_t drop_throttle;
  log_throttle_t trunc_throttle;
  log_throttle_t fail_throttle;
  log_throttle_t lag_throttle;

  unsigned char mpd_buf[DASH_MPD_BUF_CAP + 1];
  unsigned char seg_buf[DASH_SEG_BUF_CAP];

  unsigned char out_buf[DASH_OUT_BUF_CAP];
  size_t out_len;
  size_t out_pos;
};

void dash_live_emit(dash_live_t *h, const unsigned char *data, size_t len) {
  size_t room = sizeof h->out_buf - h->out_len;
  size_t n = len < room ? len : room;
  if (len % 188 == 0) n -= n % 188; /* never end on a partial TS packet */
  memcpy(h->out_buf + h->out_len, data, n);
  h->out_len += n;
  if (room < len) log_throttled(&h->drop_throttle, LOG_THROTTLE_WINDOW_S, "LW100121", "input %u (%s): DASH output buffer full, dropped bytes", h->idx, h->label ? h->label : "?");
}

dash_live_t *dash_live_new(const http_url_t *mpd_url, const char *user_agent, int insecure, unsigned idx, const char *label, unsigned adaptation_set_index, const dash_insp_t *si, dash_segment_cb cb, void *cb_ctx) {
  dash_live_t *h = calloc(1, sizeof *h);
  if (!h) return NULL;
  h->mpd_url = *mpd_url;
  bufcpy(h->user_agent, sizeof h->user_agent, user_agent ? user_agent : "dvbipitools");
  h->insecure = insecure;
  h->idx = idx;
  h->label = label;
  h->adaptation_set_index = adaptation_set_index;
  if (si) h->si = *si;
  h->cb = cb;
  h->cb_ctx = cb_ctx;
  h->phase = DASH_LIVE_IDLE;
  return h;
}

void dash_live_free(dash_live_t *h) {
  if (!h) return;
  if (h->fetch) http_fetch_free(h->fetch);
  if (h->reuse) http_close(h->reuse);
  free(h);
}

int dash_live_poll_fd(const dash_live_t *h) { return h->fetch ? http_fetch_poll_fd(h->fetch) : -1; }

short dash_live_poll_events(const dash_live_t *h) { return h->fetch ? http_fetch_poll_events(h->fetch) : 0; }

int dash_live_has_buffered(const dash_live_t *h) {
  double now;
  if (h->out_pos < h->out_len) return 1;
  if (h->fetch) return 0;
  now = mono_seconds();
  if (!h->have_representation) return now >= h->next_mpd_poll_at;
  if (now >= h->next_segment_at) return 1;
  return now >= h->next_mpd_poll_at;
}

static int start_mpd_fetch(dash_live_t *h) {
  http_t *reuse = http_take_reuse(&h->reuse, &h->reuse_established_at, DASH_MAX_REUSE_AGE_S);
  h->mpd_fetched_at = mono_seconds();
  h->fetch = http_fetch_start(&h->mpd_url, h->user_agent, h->insecure, NULL, h->etag[0] ? h->etag : NULL, h->mpd_buf, sizeof h->mpd_buf - 1, reuse, NULL);
  h->phase = DASH_LIVE_FETCHING_MPD;
  return h->fetch != NULL;
}

static int start_url_fetch(dash_live_t *h, const char *url, dash_live_phase_t phase) {
  http_url_t u;
  http_t *reuse = http_take_reuse(&h->reuse, &h->reuse_established_at, DASH_MAX_REUSE_AGE_S);
  if (http_url_parse(url, &u)) {
    if (reuse) http_close(reuse);
    return 0;
  }
  h->fetch = http_fetch_start(&u, h->user_agent, h->insecure, NULL, NULL, h->seg_buf, sizeof h->seg_buf, reuse, NULL);
  h->phase = phase;
  return h->fetch != NULL;
}

static int uses_timeline(const dash_live_t *h) { return h->repr.n_timeline > 0; }

static int wall_anchored(const dash_live_t *h) { return h->mpd.is_dynamic && h->mpd.has_availability_start && !uses_timeline(h) && h->repr.duration; }

static int paced(const dash_live_t *h) { return !wall_anchored(h) && !(uses_timeline(h) && h->mpd.is_dynamic); }

static double timescale_s(const dash_representation_t *r, unsigned long long ticks) { return (double)ticks / (double)(r->timescale ? r->timescale : 1); }

static double segment_duration_s(const dash_live_t *h) {
  if (uses_timeline(h) && h->seg_d) return timescale_s(&h->repr, h->seg_d);
  if (h->repr.duration) return timescale_s(&h->repr, h->repr.duration);
  return DASH_DEFAULT_SEGMENT_DURATION_S;
}

static unsigned long long elapsed_ms(const dash_live_t *h) {
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

static int find_period(const dash_live_t *h, unsigned *out) {
  for (unsigned i = 0; i < h->mpd.n_periods; i++) {
    if (h->mpd.periods[i].start_ms == h->cur_period_start_ms) {
      *out = i;
      return 1;
    }
  }
  return 0;
}

static int pick_representation(dash_live_t *h, unsigned period) {
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

static void join_position(dash_live_t *h) {
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

static void switch_period_if_due(dash_live_t *h) {
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

typedef enum { PREP_WAIT, PREP_READY, PREP_NEED_MPD } prep_t;

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

static prep_t prepare_segment(dash_live_t *h, char *url, size_t n) {
  switch_period_if_due(h);
  return uses_timeline(h) ? prepare_timeline(h, url, n) : prepare_duration(h, url, n);
}

static void advance_segment(dash_live_t *h) {
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

static int handle_mpd_done(dash_live_t *h, net_err_reason_t *reason_out) {
  size_t len;
  int status;
  int truncated;
  char etag[DASH_ETAG_MAX];
  unsigned period;

  http_fetch_take(h->fetch, &len, &status, etag, sizeof etag, &truncated, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  if (truncated) {
    log_code_ansi("LE400230", "input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mDASH MPD exceeds %d bytes\e[0m", h->idx, h->label ? h->label : "?", DASH_MPD_BUF_CAP);
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (status == 304 || len == 0) {
    h->next_mpd_poll_at = mono_seconds() + (h->update_period_s > 0.0 ? h->update_period_s : DASH_DEFAULT_UPDATE_PERIOD_S);
    if (h->wait_timeline) {
      h->wait_timeline = 0;
      h->next_segment_at = mono_seconds();
    }
    return 1;
  }

  h->mpd_buf[len] = '\0';
  if (!dash_mpd_parse((char *)h->mpd_buf, &h->mpd_url, &h->mpd)) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (etag[0]) bufcpy(h->etag, sizeof h->etag, etag);
  if (h->positioned && find_period(h, &period)) {
    if (!pick_representation(h, period)) {
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return -1;
    }
  } else {
    h->positioned = 0;
    if (!pick_representation(h, dash_mpd_live_period(&h->mpd))) {
      if (reason_out) *reason_out = NET_ERR_FORMAT;
      return -1;
    }
    join_position(h);
  }

  h->update_period_s = h->mpd.is_dynamic && h->mpd.minimum_update_period_ms ? (double)h->mpd.minimum_update_period_ms / 1000.0 : DASH_DEFAULT_UPDATE_PERIOD_S;
  h->next_mpd_poll_at = mono_seconds() + h->update_period_s;
  if (h->wait_timeline) {
    h->wait_timeline = 0;
    h->next_segment_at = mono_seconds();
  }
  return 1;
}

static int init_failed(dash_live_t *h, net_err_reason_t *reason_out) {
  if (++h->fail_count >= DASH_MAX_FAILS) {
    log_code_ansi("LE400237", "input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mDASH init segment unavailable\e[0m", h->idx, h->label ? h->label : "?");
    if (reason_out) *reason_out = NET_ERR_HTTP;
    return -1;
  }
  h->next_segment_at = mono_seconds() + DASH_INIT_RETRY_S;
  return 1;
}

static int handle_init_done(dash_live_t *h, net_err_reason_t *reason_out) {
  size_t len;
  int status;
  int truncated;
  http_fetch_take(h->fetch, &len, &status, NULL, 0, &truncated, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  if (truncated) {
    log_code_ansi("LE400230", "input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mDASH init segment exceeds %d bytes\e[0m", h->idx, h->label ? h->label : "?", DASH_SEG_BUF_CAP);
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (status < 200 || status > 299 || len == 0) return init_failed(h, reason_out);
  h->fail_count = 0;
  h->init_fetched = 1;
  h->next_segment_at = mono_seconds();
  if (h->cb) h->cb(h->cb_ctx, h, h->seg_buf, len);
  return 1;
}

static void segment_failed(dash_live_t *h) {
  double now = mono_seconds();
  log_throttled(&h->fail_throttle, LOG_THROTTLE_WINDOW_S, "LW400235", "input %u (%s): DASH segment unavailable, retrying", h->idx, h->label ? h->label : "?");
  if (!h->mpd.is_dynamic) {
    h->next_segment_at = now + DASH_STATIC_RETRY_S;
  } else if (++h->fail_count >= DASH_MAX_FAILS) {
    join_position(h);
    h->etag[0] = '\0';
    h->next_mpd_poll_at = now;
    h->next_segment_at = now;
  } else {
    h->next_segment_at = now + DASH_SEG_RETRY_S;
  }
}

static int handle_segment_done(dash_live_t *h) {
  size_t len;
  int status;
  int truncated;
  http_fetch_take(h->fetch, &len, &status, NULL, 0, &truncated, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  if (truncated) {
    log_throttled(&h->trunc_throttle, LOG_THROTTLE_WINDOW_S, "LW400229", "input %u (%s): DASH segment exceeds %d bytes, skipped", h->idx, h->label ? h->label : "?", DASH_SEG_BUF_CAP);
    advance_segment(h);
    return 1;
  }
  if (status < 200 || status > 299 || len == 0) {
    segment_failed(h);
    return 1;
  }
  h->fail_count = 0;
  if (h->si.slot && h->si.level != METRICS_INSPECT_TS_OFF)
    tsinspect_grid_lazy(h->si.slot, h->si.level, h->si.known_pids, h->si.n_known_pids, h->seg_buf, len);
  if (h->cb) h->cb(h->cb_ctx, h, h->seg_buf, len);
  advance_segment(h);
  return 1;
}

ssize_t dash_live_read(dash_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->si.slot && *h->si.slot) tsinspect_tick(*h->si.slot, mono_seconds());

  if (h->phase == DASH_LIVE_IDLE) {
    int room = sizeof h->out_buf - h->out_len >= DASH_OUT_BUF_PREFETCH_HEADROOM;
    if (!h->have_representation) {
      if (mono_seconds() >= h->next_mpd_poll_at && !start_mpd_fetch(h)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    } else if (!h->init_fetched) {
      char url[2048];
      dash_init_url(&h->repr, url, sizeof url);
      if (mono_seconds() >= h->next_segment_at && room && !start_url_fetch(h, url, DASH_LIVE_FETCHING_INIT)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    } else {
      if (mono_seconds() >= h->next_segment_at) {
        char url[2048];
        if (prepare_segment(h, url, sizeof url) == PREP_READY && room && !start_url_fetch(h, url, DASH_LIVE_FETCHING_SEGMENT)) {
          if (reason_out) *reason_out = NET_ERR_OTHER;
          return -1;
        }
      }
      if (h->phase == DASH_LIVE_IDLE && mono_seconds() >= h->next_mpd_poll_at && !start_mpd_fetch(h)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    }
  }

  if (h->phase != DASH_LIVE_IDLE) {
    net_err_reason_t why = NET_ERR_OTHER;
    http_fetch_state_t st = http_fetch_step(h->fetch, &why);
    if (st == HTTP_FETCH_ERROR) {
      dash_live_phase_t failed = h->phase;
      http_fetch_free(h->fetch);
      h->fetch = NULL;
      h->phase = DASH_LIVE_IDLE;
      if (why == NET_ERR_HTTP && failed == DASH_LIVE_FETCHING_SEGMENT && h->mpd.is_dynamic) {
        segment_failed(h);
      } else if (why == NET_ERR_HTTP && failed == DASH_LIVE_FETCHING_INIT) {
        if (init_failed(h, reason_out) < 0) return -1;
      } else {
        if (reason_out) *reason_out = why;
        return -1;
      }
    }
    if (st == HTTP_FETCH_DONE) {
      int rc;
      switch (h->phase) {
        case DASH_LIVE_FETCHING_MPD:  rc = handle_mpd_done(h, reason_out); break;
        case DASH_LIVE_FETCHING_INIT: rc = handle_init_done(h, reason_out); break;
        case DASH_LIVE_FETCHING_SEGMENT:
        case DASH_LIVE_IDLE:
        default:
          rc = handle_segment_done(h);
          break;
      }
      if (rc < 0) return -1;
    }
  }

  if (h->out_pos < h->out_len) {
    size_t n = h->out_len - h->out_pos;
    if (n > cap) n = cap;
    memcpy(buf, h->out_buf + h->out_pos, n);
    h->out_pos += n;
    if (h->out_pos == h->out_len) {
      h->out_pos = 0;
      h->out_len = 0;
    }
    return (ssize_t)n;
  }
  return 0;
}
