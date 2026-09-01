/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* in-memory segment ring, one per (capture_ctx_t, pid_filter_t, pmt_pid,
   container). ts, fmp4 segmenters coexist per channel, no disk mode.
   shared by hls/ (HLS+LL-HLS) and dash/ (DASH+LL-DASH) - not specific to either */

#ifndef DIPIXY_SEGSTORE_H
#define DIPIXY_SEGSTORE_H

#include <stddef.h>
#include <stdint.h>

#include "ts/capture/capture.h"
#include "ts/pidfilter.h"
#include "reactor/conn.h"

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

#define HLS_MAX_PARTS 32

/* enables LL part tracking (LL-HLS on a TS store, LL-DASH chunking on a FMP4 store).
   part_target: target part/chunk duration seconds, drives EXT-X-PART-INF/PART-HOLD-BACK (TS) or chunk cadence (FMP4). 2nd call updates part_target */
void hls_llhls_enable(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, double part_target);

/* records one part/chunk: bytes + duration + keyframe flag (independent).
   -1 on a full part table: stop calling for rest of this segment, plain HLS/DASH still works. 0 ok, -1 also if store not open */
int hls_push_part(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, const uint8_t *data, size_t size, double duration, int independent);

/* finalizes in-progress segment: promotes accumulated hls_push_part() bytes + parts into ring. 0 ok, -1 if store not open */
int hls_push_segment_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, double duration);

/* fires after hls_push_part(), seq: enclosing (still-open) segment's seq. NULL to unregister */
typedef void (*hls_part_pushed_cb)(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, uint32_t seq, const uint8_t *data, size_t len);
void hls_set_part_pushed_cb(hls_part_pushed_cb cb);

/* fires after hls_push_segment_ll(), seq: the now-finalized segment's seq. NULL to unregister */
typedef void (*hls_segment_done_cb)(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, uint32_t seq);
void hls_set_segment_done_cb(hls_segment_done_cb cb);

/* 1 once store has its first segment, 0 if not yet or ctx unknown */
int hls_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container);

/* same, LL: 1 once store has its first part/chunk or segment */
int hls_ll_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container);

/* 1 if part want_part of segment want_seg exists, ring or in-progress.
   0 if not yet, or ctx unknown */
int hls_part_available(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, uint32_t want_seg, int want_part);

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

#endif
