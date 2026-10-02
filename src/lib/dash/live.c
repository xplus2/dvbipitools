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
  dash_representation_t repr;
  int init_fetched;
  unsigned long long next_number;
  double update_period_s;

  unsigned char mpd_buf[DASH_MPD_BUF_CAP + 1];
  unsigned char seg_buf[DASH_SEG_BUF_CAP];

  unsigned char out_buf[DASH_OUT_BUF_CAP];
  size_t out_len;
  size_t out_pos;
};

void dash_live_emit(dash_live_t *h, const unsigned char *data, size_t len) {
  size_t room = sizeof h->out_buf - h->out_len;
  size_t n = len < room ? len : room;
  memcpy(h->out_buf + h->out_len, data, n);
  h->out_len += n;
  if (room < len) log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;33mDASH output buffer full, dropped bytes\e[0m", h->idx, h->label ? h->label : "?");
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
  if (!h->init_fetched) return 1;
  if (now >= h->next_segment_at) return 1;
  return now >= h->next_mpd_poll_at;
}

static int start_mpd_fetch(dash_live_t *h) {
  http_t *reuse = http_take_reuse(&h->reuse, &h->reuse_established_at, DASH_MAX_REUSE_AGE_S);
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

static double segment_duration_s(const dash_representation_t *r) {
  if (r->timescale && r->duration) return (double)r->duration / (double)r->timescale;
  return DASH_DEFAULT_SEGMENT_DURATION_S;
}

static int handle_mpd_done(dash_live_t *h, net_err_reason_t *reason_out) {
  size_t len;
  int status;
  int truncated;
  char etag[DASH_ETAG_MAX];
  dash_mpd_t mpd;
  const dash_adaptation_set_t *as;
  int best;
  double update_period_s;

  http_fetch_take(h->fetch, &len, &status, etag, sizeof etag, &truncated, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  if (status == 304 || len == 0) {
    h->next_mpd_poll_at = mono_seconds() + (h->update_period_s > 0.0 ? h->update_period_s : DASH_DEFAULT_UPDATE_PERIOD_S);
    return 1;
  }

  h->mpd_buf[len] = '\0';
  if (!dash_mpd_parse((char *)h->mpd_buf, &h->mpd_url, &mpd)) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (etag[0]) bufcpy(h->etag, sizeof h->etag, etag);
  if (mpd.n_periods == 0 || h->adaptation_set_index >= mpd.periods[0].n_adaptation_sets) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  as = &mpd.periods[0].adaptation_sets[h->adaptation_set_index];
  best = dash_pick_highest(as);
  if (best < 0) {
    if (reason_out) *reason_out = NET_ERR_FORMAT;
    return -1;
  }
  if (!h->have_representation || strcmp(h->repr.init_url, as->representations[(unsigned)best].init_url)) h->init_fetched = 0;
  if (!h->have_representation) h->next_number = as->representations[(unsigned)best].start_number ? as->representations[(unsigned)best].start_number : 1;
  h->repr = as->representations[(unsigned)best];
  h->have_representation = 1;

  update_period_s = mpd.is_dynamic && mpd.minimum_update_period_ms ? (double)mpd.minimum_update_period_ms / 1000.0 : DASH_DEFAULT_UPDATE_PERIOD_S;
  h->update_period_s = update_period_s;
  h->next_mpd_poll_at = mono_seconds() + update_period_s;
  return 1;
}

static int handle_init_done(dash_live_t *h) {
  size_t len;
  http_fetch_take(h->fetch, &len, NULL, NULL, 0, NULL, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  h->init_fetched = 1;
  h->next_segment_at = mono_seconds();
  if (h->cb) h->cb(h->cb_ctx, h, h->seg_buf, len);
  return 1;
}

static int handle_segment_done(dash_live_t *h) {
  size_t len;
  http_fetch_take(h->fetch, &len, NULL, NULL, 0, NULL, &h->reuse);
  h->fetch = NULL;
  h->phase = DASH_LIVE_IDLE;
  if (h->si.slot && h->si.level != METRICS_INSPECT_TS_OFF)
    tsinspect_grid_lazy(h->si.slot, h->si.level, h->si.known_pids, h->si.n_known_pids, h->seg_buf, len);
  if (h->cb) h->cb(h->cb_ctx, h, h->seg_buf, len);
  h->next_number++;
  h->next_segment_at = mono_seconds() + segment_duration_s(&h->repr);
  return 1;
}

ssize_t dash_live_read(dash_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->si.slot && *h->si.slot) tsinspect_tick(*h->si.slot, mono_seconds());

  if (h->phase == DASH_LIVE_IDLE) {
    if (!h->have_representation) {
      if (mono_seconds() >= h->next_mpd_poll_at && !start_mpd_fetch(h)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    } else if (!h->init_fetched) {
      if (sizeof h->out_buf - h->out_len >= DASH_OUT_BUF_PREFETCH_HEADROOM && !start_url_fetch(h, h->repr.init_url, DASH_LIVE_FETCHING_INIT)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    } else if (mono_seconds() >= h->next_segment_at) {
      char url[2048];
      dash_media_url(&h->repr, h->next_number, 0, url, sizeof url);
      if (sizeof h->out_buf - h->out_len >= DASH_OUT_BUF_PREFETCH_HEADROOM && !start_url_fetch(h, url, DASH_LIVE_FETCHING_SEGMENT)) {
        if (reason_out) *reason_out = NET_ERR_OTHER;
        return -1;
      }
    } else if (mono_seconds() >= h->next_mpd_poll_at && !start_mpd_fetch(h)) {
      if (reason_out) *reason_out = NET_ERR_OTHER;
      return -1;
    }
  }

  if (h->phase != DASH_LIVE_IDLE) {
    http_fetch_state_t st = http_fetch_step(h->fetch, reason_out);
    if (st == HTTP_FETCH_ERROR) {
      http_fetch_free(h->fetch);
      h->fetch = NULL;
      h->phase = DASH_LIVE_IDLE;
      return -1;
    }
    if (st == HTTP_FETCH_DONE) {
      int rc;
      switch (h->phase) {
        case DASH_LIVE_FETCHING_MPD:  rc = handle_mpd_done(h, reason_out); break;
        case DASH_LIVE_FETCHING_INIT: rc = handle_init_done(h); break;
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
