/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* in-memory segment ring, one per (capture_ctx_t, pid_filter_t, pmt_pid,
   container). ts, fmp4 segmenters coexist per channel, no disk mode.
   shared by hls/ (HLS+LL-HLS) and dash/ (DASH+LL-DASH) - not specific to either */

#ifndef DIPIXY_SEGSTORE_H
#define DIPIXY_SEGSTORE_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi/psi.h"
#include "ts/capture/capture.h"
#include "ts/lcevcselect.h"
#include "ts/pidfilter.h"
#include "reactor/conn.h"
#include "reactor/qsbr.h"

#define HLS_MAX_STORES 1024
#define HLS_MAX_SEGS 10

typedef enum { SEG_CONTAINER_TS, SEG_CONTAINER_FMP4 } seg_container_t;
typedef struct hls_store_t hls_store_t;

hls_store_t *hls_store_find(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);

void hls_store_init(int max_channels);
void hls_store_set_qsbr(qsbr_domain_t *d);

/* call once at startup, before any HLS traffic. n clamped to min 1 */
void hls_set_seg_pool_cap(int n);

void hls_seg_pool_trim_idle(void);

/* reclaim closed slots, recurring maint call */
void hls_store_slot_reclaim_sweep(void);

/* open store for (ctx, filter, pmt_pid) wiped + restarted. pmt_pid: 0 = auto (first PMT that resolves), else program.
   seg_target: target seg seconds for EXT-X-TARGETDURATION. max_segs: playlist sliding-window size, clamped to [2, HLS_MAX_SEGS] */
void hls_store_open(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, double seg_target, int max_segs, seg_container_t container);

/* noop if not open */
void hls_store_close(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);

/* data copied into a malloc'd buffer. 0 ok, -1 if store not open */
int hls_push_segment(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration);
int hls_push_segment_at(hls_store_t *s, const uint8_t *data, size_t size, double duration);

/* fmp4 only, served at "init.mp4". copies data. video_codec: drives HLS VERSION + DASH codecs=.
   0 ok, -1 if store not open */
int hls_set_init_segment(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, codec_t video_codec, const uint8_t *data, size_t size);
int hls_set_init_segment_at(hls_store_t *s, codec_t video_codec, const uint8_t *data, size_t size);

/* lcevc_pid[] for base video. 0 ok, -1 store not open */
int hls_set_lcevc_pids(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const unsigned *lcevc_pid, int lcevc_pid_count);

#define HLS_MAX_PARTS 32

/* enables LL part tracking (LL-HLS on a TS store, LL-DASH chunking on a FMP4 store).
   part_target: target part/chunk duration seconds, drives EXT-X-PART-INF/PART-HOLD-BACK (TS) or chunk cadence (FMP4). 2nd call updates part_target */
void hls_llhls_enable(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double part_target);

/* records one part/chunk: bytes + duration + keyframe flag (independent).
   -1 on a full part table: stop calling for rest of this segment, plain HLS/DASH still works. 0 ok, -1 also if store not open */
int hls_push_part(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration, int independent);
int hls_push_part_at(hls_store_t *s, const uint8_t *data, size_t size, double duration, int independent);

/* finalizes in-progress segment: promotes accumulated hls_push_part() bytes + parts into ring. 0 ok, -1 if store not open */
int hls_push_segment_ll(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double duration);
int hls_push_segment_ll_at(hls_store_t *s, double duration);

/* fires after hls_push_part(), seq: enclosing (still-open) segment's seq. NULL to unregister */
typedef void (*hls_part_pushed_cb)(const hls_store_t *store, uint32_t seq, const uint8_t *data, size_t len);
void hls_set_part_pushed_cb(hls_part_pushed_cb cb);

/* fires after hls_push_segment_ll(), seq: the now-finalized segment's seq. NULL to unregister */
typedef void (*hls_segment_done_cb)(const hls_store_t *store, uint32_t seq);
void hls_set_segment_done_cb(hls_segment_done_cb cb);

typedef void (*hls_store_closing_cb)(const hls_store_t *store);
void hls_set_store_closing_cb(hls_store_closing_cb cb);

typedef void (*hls_init_codecs_fn)(const uint8_t *init, size_t initsz, codec_t vcodec, char *vcodec_out, size_t vcodec_outsz, char *acodec_out, size_t acodec_outsz);
void hls_set_init_codecs_cb(hls_init_codecs_fn cb);

/* 1 once store has its first segment, 0 if not yet or ctx unknown */
int hls_store_ready(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);

/* same, LL: 1 once store has its first part/chunk or segment */
int hls_ll_store_ready(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);

/* 1 if part want_part of segment want_seg exists, ring or in-progress.
   0 if not yet, or ctx unknown */
int hls_part_available(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, uint32_t want_seg, int want_part);

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
