/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/dash/live.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/dash/live/live_int.h"

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

int dash_live_finished(const dash_live_t *h) { return h->finished && h->out_pos >= h->out_len; }

int dash_live_has_buffered(const dash_live_t *h) {
  double now;
  if (h->out_pos < h->out_len || h->finished) return 1;
  if (h->fetch) return 0;
  now = mono_seconds();
  if (!h->have_representation) return now >= h->next_mpd_poll_at;
  if (now >= h->next_segment_at) return 1;
  return now >= h->next_mpd_poll_at;
}
