/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DASH_LIVE_H
#define DVBIPITOOLS_LIB_DASH_LIVE_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/net/httpclient/httpclient.h"
#include "lib/tsinspect/inspect.h"

typedef struct dash_live dash_live_t;

typedef struct {
  tsinspect_t **slot;
  metrics_inspect_ts_t level;
  const unsigned *known_pids;
  unsigned n_known_pids;
} dash_insp_t;

typedef void (*dash_segment_cb)(void *ctx, dash_live_t *h, const unsigned char *data, size_t len);

dash_live_t *dash_live_new(const http_url_t *mpd_url, const char *user_agent, int insecure, unsigned idx, const char *label, unsigned adaptation_set_index, const dash_insp_t *si, dash_segment_cb cb, void *cb_ctx);

void dash_live_free(dash_live_t *h);

int dash_live_poll_fd(const dash_live_t *h);
short dash_live_poll_events(const dash_live_t *h);

ssize_t dash_live_read(dash_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

int dash_live_has_buffered(const dash_live_t *h);

void dash_live_emit(dash_live_t *h, const unsigned char *data, size_t len);

#endif
