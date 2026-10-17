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

int start_mpd_fetch(dash_live_t *h) {
  http_t *reuse = http_take_reuse(&h->reuse, &h->reuse_established_at, DASH_MAX_REUSE_AGE_S);
  h->mpd_fetched_at = mono_seconds();
  h->fetch = http_fetch_start(&h->mpd_url, h->user_agent, h->insecure, NULL, h->etag[0] ? h->etag : NULL, h->mpd_buf, sizeof h->mpd_buf - 1, reuse, NULL);
  h->phase = DASH_LIVE_FETCHING_MPD;
  return h->fetch != NULL;
}

int start_url_fetch(dash_live_t *h, const char *url, dash_live_phase_t phase) {
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

int handle_mpd_done(dash_live_t *h, net_err_reason_t *reason_out) {
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

int init_failed(dash_live_t *h, net_err_reason_t *reason_out) {
  if (++h->fail_count >= DASH_MAX_FAILS) {
    log_code_ansi("LE400237", "input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): \e[0;31mDASH init segment unavailable\e[0m", h->idx, h->label ? h->label : "?");
    if (reason_out) *reason_out = NET_ERR_HTTP;
    return -1;
  }
  h->next_segment_at = mono_seconds() + DASH_INIT_RETRY_S;
  return 1;
}

int handle_init_done(dash_live_t *h, net_err_reason_t *reason_out) {
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

void segment_failed(dash_live_t *h) {
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

int handle_segment_done(dash_live_t *h) {
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
