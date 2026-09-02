/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DASH_H
#define DIPIXY_DASH_H

#include "../segstore.h"

/* MPD manifest only. init.mp4 routes through hls_serve() instead, same filename. SEG_CONTAINER_FMP4 stores only.
   want_ll: ROUTE_FMT_LLDASH vs ROUTE_FMT_DASH. utc_url: UTCTiming/ProducerReferenceTime source, want_ll only.
   0 no store/not fmp4/no segments yet, 1 handled */
int dash_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int want_ll, const char *utc_url, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes);

/* DASH media segment, filename "dsegTTTT.m4s" (TTTT: start time ms, from the MPD's own SegmentTimeline).
   key by time. 0 no store/not fmp4/no match, 1 handled */
int dash_serve_seg(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes);

/* same lookup as dash_serve() */
int dash_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int want_ll, const char *utc_url, int is_head, hls_resp_t *out);

/* same lookup as dash_serve_seg() */
int dash_render_seg(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, hls_resp_t *out);

#endif
