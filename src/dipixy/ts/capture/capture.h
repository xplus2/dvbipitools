/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CAPTURE_H
#define DIPIXY_CAPTURE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/helper/sds_xml.h"
#include "lib/tsinspect/inspect.h"

typedef struct capture_ctx capture_ctx_t;

#define CAPTURE_PUMP_MAX_THREADS 32

/* main() only, before any capture_open/capture_rist_init/capture_stdin_init call. NULL: inspect-ts off */
void capture_set_inspect(tsinspect_agg_t *agg);

/* 1 join per unique (family, group, source, port, iface). repeat calls share it (refcount).
   source: SSM source, NULL or "" = ASM. rtp: nonzero unwraps RTP before buffer. ret/fcc: NULL = none */
capture_ctx_t *capture_open(int family, const char *group, const char *source, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc, const sds_fec_t *fec, unsigned al_fec_l, unsigned al_fec_d);

/* last ref: unlink, free deferred past pump quiesc. */
void capture_close(capture_ctx_t *ctx);

typedef struct capture_open_job capture_open_job_t;

/* reactor_run() only, after capture_pump_set_thread_count(). wake(tid): job with that wake_tid done, lifecycle thread context.
   -1: no thread, close/open stay synchronous */
int capture_lifecycle_start(void (*wake)(int tid));

/* reactor_run() only, after pump threads joined */
void capture_lifecycle_stop(void);

/* capture_open() dedup, never blocks. ref++ or NULL */
capture_ctx_t *capture_try_ref(int family, const char *group, const char *source, unsigned port, const char *iface);
capture_ctx_t *capture_try_ref_srt(const char *host, unsigned port);
capture_ctx_t *capture_try_ref_http(const char *url);

/* 1: lifecycle thread accepts jobs. 0: caller must open in sync */
int capture_lifecycle_active(void);

/* capture_open()/capture_open_srt() on lifecycle thread, params copied. NULL: not queued (queue full, bad params, or lifecycle inactive) */
capture_open_job_t *capture_open_async(int wake_tid, int family, const char *group, const char *source, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc, const sds_fec_t *fec, unsigned al_fec_l, unsigned al_fec_d);
capture_open_job_t *capture_open_srt_async(int wake_tid, const char *host, unsigned port);

/* capture_open_http_static() on lifecycle thread. NULL also if url failed in last 5 s */
capture_open_job_t *capture_open_http_async(int wake_tid, const char *url, int insecure_tls);

int capture_open_job_done(const capture_open_job_t *job);

/* done jobs only. owned ref, NULL: failed. frees job */
capture_ctx_t *capture_open_job_take(capture_open_job_t *job);

/* any state. frees job, closes result when ready */
void capture_open_job_cancel(capture_open_job_t *job);

extern _Thread_local int t_pump_tid;

typedef void (*capture_sink_fn)(capture_ctx_t *ctx, void *user, const unsigned char *pkts, size_t n);

/* drain pending 2 sink. packets delivered, -1 fatal read error */
int capture_drain(capture_ctx_t *ctx, capture_sink_fn sink, void *user);

/* reactor_run() only, before spawning any pump/worker threads. clamps to
   [1, CAPTURE_PUMP_MAX_THREADS] */
void capture_pump_set_thread_count(int n);
int capture_pump_thread_count(void);

void capture_pump_wait(int pid, int max_ms);

/* CAPTURE_PUMP_MAX_THREADS entries */
void capture_pump_mark(uint64_t *mark);

/* all pumps quiescent since mark */
int capture_pump_mark_passed(const uint64_t *mark);

/* drains pid's shard once. 0 return: shard idle, caller can sleep */
int capture_pump_tick(int pid, capture_sink_fn sink, void *user);

/* ts_push.c/hls: blocks (yields, not spins) until no pump thread mid-feed.
   safe to free memread unlocked mid-feed */
void capture_wait_pumps_quiescent(void);

typedef void (*qsbr_deferred_fn)(void *arg);

int capture_defer_after_quiescent(qsbr_deferred_fn fn, void *arg);

void capture_flush_deferred_quiescent(void);

/* count of distinct (family, group, source, port, iface) joins currently open */
int capture_active_count(void);

/* monotonic B read across all sources */
uint64_t capture_bytes_total(void);

/* B written. 0: igmp ok, but nothing arrives */
uint64_t capture_ctx_bytes(const capture_ctx_t *ctx);

/* librist: one ctx per process, ever. NULL uri = off. 0/-1 ok/failed, logged */
int capture_rist_init(const char *rist_uri);

/* returns rist ctx, ref++. NULL: --rist unset */
capture_ctx_t *capture_rist_get(void);

/* one call, ever. 0/-1 ok/failed, logged */
int capture_stdin_init(void);

/* returns stdin ctx, ref++. NULL: -i - unset */
capture_ctx_t *capture_stdin_get(void);

/* caller-mode SRT, dedup by host:port, ref++ like capture_open() */
capture_ctx_t *capture_open_srt(const char *host, unsigned port);

/* http source, dedup by url, ref++ like capture_open(). NULL: failed, logged */
capture_ctx_t *capture_open_http_static(const char *url, int insecure_tls);

/* ref++, close later via capture_close() */
capture_ctx_t *capture_ref(capture_ctx_t *ctx);

/* ts_push.c only. address of ctx's subscriber-chain head: index into g_ts_subs[], -1 none */
_Atomic int *capture_ts_push_head_ptr(capture_ctx_t *ctx);

_Atomic(void *) *capture_hls_seg_head_ptr(capture_ctx_t *ctx);

#endif
