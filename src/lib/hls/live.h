/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HLS_LIVE_H
#define DVBIPITOOLS_LIB_HLS_LIVE_H

#include <stddef.h>
#include <sys/types.h>

#include "lib/net/httpclient/httpclient.h"
#include "lib/tsinspect/inspect.h"

typedef struct hls_live hls_live_t;

typedef struct {
  tsinspect_t **slot;
  metrics_inspect_ts_t level;
  const unsigned *known_pids;
  unsigned n_known_pids;
} hls_insp_t;

typedef enum { HLS_SEG_UNKNOWN, HLS_SEG_TS, HLS_SEG_PACKED_AUDIO, HLS_SEG_FMP4 } hls_segment_kind_t;

typedef void (*hls_segment_cb)(void *ctx, hls_live_t *h, const unsigned char *data, size_t len);

typedef int (*hls_init_cb)(void *ctx, const hls_live_t *h, const unsigned char *data, size_t len);

hls_live_t *hls_live_new(const http_url_t *playlist_url, const char *user_agent, int insecure, unsigned idx, const char *label, const hls_insp_t *si, hls_segment_cb cb, void *cb_ctx);

/* fMP4 init segment (EXT-X-MAP) fetched before the first segment and whenever its URI changes. cb returns 0 on failure */
void hls_live_set_init_cb(hls_live_t *h, hls_init_cb cb, void *ctx);

void hls_live_free(hls_live_t *h);

int hls_live_poll_fd(const hls_live_t *h);
short hls_live_poll_events(const hls_live_t *h);

ssize_t hls_live_read(hls_live_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out);

int hls_live_has_buffered(const hls_live_t *h);

void hls_live_emit(hls_live_t *h, const unsigned char *data, size_t len);

hls_segment_kind_t hls_live_segment_kind(const unsigned char *data, size_t len);

int hls_live_uses_fmp4(const hls_live_t *h);

#endif
