/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/metrics/export.h"
#include "lib/net/retryset.h"

#include "inputset.h"

#define INPUTSET_PMT_PID_BASE 0x1000   /* matches ffmpeg mpegts muxer's pmt_start_pid default */
#define INPUTSET_AUDIO_PID_BASE 0x0100 /* matches ffmpeg mpegts muxer's start_pid default */

typedef struct {
  const char *uri;
  int insecure;
  source_meta_cb cb;
  void *meta_ctx;
  input_metrics_t *im;
} slot_ctx_t;

/* retryset_ops_t's open_step() only gets the opening handle, not slot_ctx.
   wrap it to carry im too */
typedef struct {
  source_open_t *o;
  input_metrics_t *im;
} slot_opening_t;

typedef struct {
  unsigned sid;
  const char *service_name;
  unsigned pmt_pid, audio_pid;
} slot_meta_t;

struct inputset {
  retryset_t *rs;
  slot_ctx_t ctxs[RADIOHEAD_MAX_INPUTS];
  slot_meta_t meta[RADIOHEAD_MAX_INPUTS];
  const char *labels[RADIOHEAD_MAX_INPUTS];
};

static void *slot_open_start(void *ctx) {
  slot_ctx_t *c = ctx;
  net_err_reason_t reason = NET_ERR_OTHER;
  slot_opening_t *w = calloc(1, sizeof *w);
  if (!w)
    return NULL;
  w->im = c->im;
  w->o = source_open_async_start(c->uri, c->insecure, c->cb, c->meta_ctx, &reason);
  if (!w->o) {
    if (c->im)
      c->im->errors_total[reason]++;
    free(w);
    return NULL;
  }
  return w;
}

static int slot_open_poll_fd(const void *o) { return source_open_async_poll_fd(((const slot_opening_t *)o)->o); }

static short slot_open_poll_events(const void *o) {
  return source_open_async_poll_events(((const slot_opening_t *)o)->o);
}

static retryset_open_state_t slot_open_step(void *o) {
  slot_opening_t *w = o;
  net_err_reason_t reason = NET_ERR_OTHER;
  switch (source_open_async_step(w->o, &reason)) {
  case SOURCE_OPEN_DONE:
    if (w->im) {
      if (w->im->seen_open)
        w->im->reconnects_total++;
      w->im->seen_open = 1;
      w->im->up = 1;
    }
    return RETRYSET_OPEN_DONE;
  case SOURCE_OPEN_ERROR:
    if (w->im)
      w->im->errors_total[reason]++;
    return RETRYSET_OPEN_ERROR;
  default:
    return RETRYSET_OPEN_PENDING;
  }
}

static void *slot_open_take(void *o) {
  slot_opening_t *w = o;
  source_t *s = source_open_async_take(w->o);
  free(w);
  return s;
}
static void slot_open_free(void *o) {
  slot_opening_t *w = o;
  if (w) {
    source_open_async_free(w->o);
    free(w);
  }
}
static int slot_result_fd(const void *r) { return source_fd((const source_t *)r); }
static void slot_result_close(void *r) { source_close((source_t *)r); }

static const retryset_ops_t slot_ops = {
  slot_open_start,  slot_open_poll_fd, slot_open_poll_events,
  slot_open_step,   slot_open_take,    slot_open_free,
  slot_result_fd,   slot_result_close};

inputset_t *inputset_new(const config_t *cfg, source_meta_cb cb, void *const *ctxs, input_metrics_t *input_stats) {
  inputset_t *is = calloc(1, sizeof *is);
  void *slot_ctxs[RADIOHEAD_MAX_INPUTS];

  if (!is)
    return NULL;

  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    is->ctxs[i].uri = cfg->inputs[i].uri;
    is->ctxs[i].insecure = cfg->insecure_tls;
    is->ctxs[i].cb = cb;
    is->ctxs[i].meta_ctx = ctxs ? ctxs[i] : NULL;
    is->ctxs[i].im = input_stats ? &input_stats[i] : NULL;
    is->meta[i].sid = cfg->inputs[i].sid;
    is->meta[i].service_name = cfg->inputs[i].sdt_text;
    is->meta[i].pmt_pid = INPUTSET_PMT_PID_BASE + i;
    is->meta[i].audio_pid = INPUTSET_AUDIO_PID_BASE + i;
    is->labels[i] = is->meta[i].service_name;
    slot_ctxs[i] = &is->ctxs[i];
  }

  is->rs = retryset_new(cfg->n_inputs, slot_ctxs, is->labels, &slot_ops, cfg->error_retry_s);
  if (!is->rs) {
    free(is);
    return NULL;
  }
  return is;
}

void inputset_free(inputset_t *is) {
  if (!is)
    return;
  retryset_free(is->rs);
  free(is);
}

unsigned inputset_count(const inputset_t *is) { return retryset_count(is->rs); }
unsigned inputset_sid(const inputset_t *is, unsigned idx) { return is->meta[idx].sid; }
const char *inputset_service_name(const inputset_t *is, unsigned idx) { return is->meta[idx].service_name; }
unsigned inputset_pmt_pid(const inputset_t *is, unsigned idx) { return is->meta[idx].pmt_pid; }
unsigned inputset_audio_pid(const inputset_t *is, unsigned idx) { return is->meta[idx].audio_pid; }

source_t *inputset_source(const inputset_t *is, unsigned idx) {
  return (source_t *)retryset_result(is->rs, idx);
}

int inputset_poll_fd(const inputset_t *is, unsigned idx) { return retryset_poll_fd(is->rs, idx); }
short inputset_poll_events(const inputset_t *is, unsigned idx) { return retryset_poll_events(is->rs, idx); }
time_t inputset_next_deadline(const inputset_t *is) { return retryset_next_deadline(is->rs); }
void inputset_service(inputset_t *is, unsigned idx, time_t now) { retryset_service(is->rs, idx, now); }
void inputset_mark_down(inputset_t *is, unsigned idx, time_t now) { retryset_mark_down(is->rs, idx, now); }
