/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HLS + LL-HLS serving. Generic segment store engine: ../segstore.h */

#ifndef DIPIXY_HLS_H
#define DIPIXY_HLS_H

#include "../segstore.h"

/* filename like: "index.m3u8", "segNNNNN.ts", "init.mp4", or "segNNNNN.m4s" queues HTTP/1.1-style status line + headers + body via conn_queue(),
   if_none_match: raw If-None-Match header value or NULL, checked against init.mp4/segments only.
   origin_hdr: raw Origin request header value or NULL, drives Access-Control-Allow-Origin.
   out_bytes: response byte count, NULL ok. 1 handled, 0 no HLS filename */
int hls_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container,
              const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes);

/* filename is bare: "index_ll.m3u8" or "segNNNNN.PP.ts". same conn_queue()/
   keep_alive/if_none_match/origin_hdr/out_bytes convention as hls_serve() (if_none_match checked against parts, not index_ll.m3u8) */
int hls_serve_ll(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes);

/* same lookup as hls_serve() */
int hls_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out);

/* same lookup as hls_serve_ll() */
int hls_render_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out);

#endif
