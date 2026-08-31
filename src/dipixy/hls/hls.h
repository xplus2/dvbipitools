/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* in-memory HLS segment ring, one per (capture_ctx_t, pid_filter_t, pmt_pid,
   container). ts, fmp4 segmenters coexist per channel, no disk mode */

#ifndef DIPIXY_HLS_H
#define DIPIXY_HLS_H

#include <stddef.h>
#include <stdint.h>

#include "../ts/capture/capture.h"
#include "../ts/pidfilter.h"
#include "../reactor/conn.h"

#define HLS_MAX_STORES 32
#define HLS_MAX_SEGS 10

typedef enum { HLS_CONTAINER_TS, HLS_CONTAINER_FMP4 } hls_container_t;

/* call once at startup, before any HLS traffic. n clamped to min 1 */
void hls_set_seg_pool_cap(int n);

void hls_seg_pool_trim_idle(void);

/* open store for (ctx, filter, pmt_pid) wiped + restarted. pmt_pid: 0 = auto (first PMT that resolves), else program.
   seg_target: target seg seconds for EXT-X-TARGETDURATION. max_segs: playlist sliding-window size, clamped to [2, HLS_MAX_SEGS] */
void hls_store_open(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, double seg_target, int max_segs, hls_container_t container);

/* noop if not open */
void hls_store_close(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container);

/* data copied into a malloc'd buffer. 0 ok, -1 if store not open */
int hls_push_segment(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, const uint8_t *data, size_t size, double duration);

/* fmp4 only, served at "init.mp4". copies data. 0 ok, -1 if store not open */
int hls_set_init_segment(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, const uint8_t *data, size_t size);

/* filename like: "index.m3u8", "segNNNNN.ts", "init.mp4", or "segNNNNN.m4s" queues HTTP/1.1-style status line + headers + body via conn_queue(),
   if_none_match: raw If-None-Match header value or NULL, checked against init.mp4/segments only.
   origin_hdr: raw Origin request header value or NULL, drives Access-Control-Allow-Origin.
   out_bytes: response byte count, NULL ok. 1 handled, 0 no HLS filename */
int hls_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container,
              const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr,
              size_t *out_bytes);

#define HLS_MAX_PARTS 32

/* enables LL-HLS part tracking, HLS_CONTAINER_TS stores only.
   part_target: target part duration seconds, drives EXT-X-PART-INF/PART-HOLD-BACK.
   2nd call updates part_target */
void hls_llhls_enable(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, double part_target);

/* records one part: bytes + duration + keyframe flag (independent).
   -1 on a full part table: stop calling for rest of this segment, plain
   HLS still works. 0 ok, -1 also if store not open */
int hls_push_part(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const uint8_t *data, size_t size, double duration, int independent);

/* finalizes in-progress segment: promotes accumulated hls_push_part() bytes + parts into ring. 0 ok, -1 if store not open */
int hls_push_segment_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, double duration);

/* 1 once store has its first segment, 0 if not yet or ctx unknown */
int hls_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container);

/* same, LL-HLS: 1 once store has its first part or segment */
int hls_ll_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid);

/* 1 if part want_part of segment want_seg exists, ring or in-progress.
   0 if not yet, or ctx unknown */
int hls_part_available(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, uint32_t want_seg, int want_part);

/* filename is bare: "index_ll.m3u8" or "segNNNNN.PP.ts". same conn_queue()/
   keep_alive/if_none_match/origin_hdr/out_bytes convention as hls_serve() (if_none_match checked against parts, not index_ll.m3u8) */
int hls_serve_ll(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes);

/* MPD manifest only. init.mp4 routes through hls_serve() instead, same filename. HLS_CONTAINER_FMP4 stores only.
   0 no store/not fmp4/no segments yet, 1 handled */
int hls_serve_dash(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes);

/* DASH media segment, filename "dsegTTTT.m4s" (TTTT: start time ms, from the MPD's own SegmentTimeline).
   key by time. 0 no store/not fmp4/no match, 1 handled */
int hls_serve_dash_seg(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes);

/* body: outlives store, release via hls_resp_body_release(body, zc) once sent */
typedef struct {
  int status;                /* 200, 304, 404 */
  const char *content_type;  /* NULL for 304/404 */
  char etag[48];             /* empty string: no ETag on this response */
  uint8_t *body;             /* NULL for 304/404 or a HEAD request */
  size_t body_len;
  int zc;                    /* 1: body is a seg_buf ref, not a private copy */
} hls_resp_t;

/* releases *out's body. safe on NULL. call exactly once */
void hls_resp_body_release(uint8_t *body, int zc);

/* same lookup as hls_serve() */
int hls_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out);

/* same lookup as hls_serve_ll() */
int hls_render_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out);

/* same lookup as hls_serve_dash() */
int hls_render_dash(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int is_head, hls_resp_t *out);

/* same lookup as hls_serve_dash_seg() */
int hls_render_dash_seg(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, hls_resp_t *out);

#endif
