/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/dash/live.h"
#include "lib/sys/signal.h"
#include "lib/dash/mpd.h"
#include "lib/dash/live/live_int.h"

ssize_t dash_live_read(dash_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->si.slot && *h->si.slot) tsinspect_tick(*h->si.slot, mono_seconds());

  if (h->phase == DASH_LIVE_IDLE && !h->finished) {
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
