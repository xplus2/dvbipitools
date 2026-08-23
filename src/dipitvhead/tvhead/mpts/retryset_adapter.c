/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "priv.h"

/* carries im: open_step() only sees the opening handle */
typedef struct {
  tvsrc_open_t *o;
  input_metrics_t *im;
} tv_opening_t;

static void *tv_open_start(void *ctx) {
  tv_slot_ctx_t *sc = ctx;
  net_err_reason_t reason = NET_ERR_OTHER;
  tv_opening_t *w = calloc(1, sizeof *w);
  if (!w)
    return NULL;
  w->im = sc->im;
  w->o = tvsrc_open_async_start(sc->cfg, sc->input, &reason);
  if (!w->o) {
    if (sc->im)
      sc->im->errors_total[reason]++;
    free(w);
    return NULL;
  }
  return w;
}
static int tv_open_poll_fd(const void *o) { return tvsrc_open_async_poll_fd(((const tv_opening_t *)o)->o); }
static short tv_open_poll_events(const void *o) { return tvsrc_open_async_poll_events(((const tv_opening_t *)o)->o); }
static retryset_open_state_t tv_open_step(void *o) {
  tv_opening_t *w = o;
  net_err_reason_t reason = NET_ERR_OTHER;
  switch (tvsrc_open_async_step(w->o, &reason)) {
  case TVSRC_OPEN_DONE:
    if (w->im) {
      if (w->im->seen_open)
        w->im->reconnects_total++;
      w->im->seen_open = 1;
      w->im->up = 1;
    }
    return RETRYSET_OPEN_DONE;
  case TVSRC_OPEN_ERROR:
    if (w->im)
      w->im->errors_total[reason]++;
    return RETRYSET_OPEN_ERROR;
  default:
    return RETRYSET_OPEN_PENDING;
  }
}
static void *tv_open_take(void *o) {
  tv_opening_t *w = o;
  tvsrc_t *r = tvsrc_open_async_take(w->o);
  free(w);
  return r;
}
static void tv_open_free(void *o) {
  tv_opening_t *w = o;
  if (w) {
    tvsrc_open_async_free(w->o);
    free(w);
  }
}
static int tv_result_fd(const void *r) { return tvsrc_fd(r); }
static void tv_result_close(void *r) { tvsrc_close((tvsrc_t *)r); }
const retryset_ops_t tv_retry_ops = {tv_open_start, tv_open_poll_fd, tv_open_poll_events, tv_open_step,
                                     tv_open_take, tv_open_free,tv_result_fd, tv_result_close};
