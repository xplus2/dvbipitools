/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CAPTURE_H
#define DIPIXY_CAPTURE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/helper/sds_xml.h"

typedef struct capture_ctx capture_ctx_t;
typedef struct capture_reader capture_reader_t;

#define CAPTURE_PUMP_MAX_THREADS 32

/* main() only, before any capture_open/capture_rist_init/capture_stdin_init call */
void capture_set_ring_cap(size_t bytes);

/* 1 join per unique (family, group, port, iface). repeat calls share it (refcount).
   rtp: nonzero unwraps RTP before buffer. ret/fcc: NULL = none */
capture_ctx_t *capture_open(int family, const char *group, unsigned port, const char *iface, int rtp, const sds_ret_t *ret, const sds_fcc_t *fcc);

void capture_close(capture_ctx_t *ctx);

/* drain pending into ring. 0 ok, -1 fatal read error */
int capture_service(capture_ctx_t *ctx);

/* drain pending, calling sink(user, pkt, 188) once per whole 188 B. 0 ok, -1 fatal read error */
int capture_drain(capture_ctx_t *ctx, void (*sink)(void *user, const unsigned char *pkt), void *user);

/* independent read cursor, starting from ctx's current write position.
   many readers can watch one ctx concurrently, each at their own pace. */
capture_reader_t *capture_reader_open(capture_ctx_t *ctx);

void capture_reader_close(capture_reader_t *r);

/* pulls up to cap bytes not yet seen by this reader, oldest first. returns bytes copied */
size_t capture_reader_read(capture_reader_t *r, unsigned char *buf, size_t cap);

/* cumulative bytes this reader lost by falling more than ring capacity behind */
size_t capture_reader_dropped(const capture_reader_t *r);

/* reactor_run() only, before spawning any pump/worker threads. clamps to
   [1, CAPTURE_PUMP_MAX_THREADS] */
void capture_pump_set_thread_count(int n);

/* drains pid's shard once, sink(source, user, pkt) per packet.
   0 return: shard idle, caller can sleep */
int capture_pump_tick(int pid, void (*sink)(capture_ctx_t *ctx, void *user, const unsigned char *pkt), void *user);

/* ts_push.c/hls: blocks (yields, not spins) until no pump thread mid-feed.
   safe to free memread unlocked mid-feed */
void capture_wait_pumps_quiescent(void);

/* count of distinct (family, group, port, iface) joins currently open */
int capture_active_count(void);

/* sum of bytes ever written across every currently open source */
uint64_t capture_bytes_total(void);

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
