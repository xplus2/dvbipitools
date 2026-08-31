/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* ts_push.h - HTTP MPEG-TS push: subscribers match by capture_ctx_t*.
   threads:
     pump: ts_push_feed_pkt()
     reactor H1: conn_send_buffered() -> CONN_TSPUSH state machine
     reactor H2: SPSC ring + per-reactor efd -> h2_tspush_wake() -> nghttp2 read_cb pulls ring
     reactor H3: SPSC ring + per-reactor efd -> ts_push_h3_flush() */

#ifndef DIPIXY_TS_PUSH_H
#define DIPIXY_TS_PUSH_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "capture/capture.h"
#include "pidfilter.h"
#include "rawaudio.h"
#include "lib/demux/psi/psi.h"
#include "../ws/ws_clients.h"

#define TS_PUSH_MAX_SUBS 4096
#define TS_PUSH_MAX_REACTOR_THREADS 32
/* byte rings, not packet-count. ts/spts always push 188B/call. rawaudio
   push variable length. capacity stays ^2, wrap bitmask */
#define TS_RING_H3_BYTES (1u << 16) /* 64 KiB, was 256 * 188 B (~48.1 KiB) */
#define TS_RING_H2_BYTES (1u << 16) /* 64 KiB, per-stream ring like h3_ring */
#define TS_RING_PUSH_BYTES (1u << 20) /* 1 MiB */

#define TS_SUB_FREE 0
#define TS_SUB_ALIVE 1
#define TS_SUB_CLOSING 2

typedef struct {
  _Atomic(capture_ctx_t *) ctx; /* pump reads unlocked past alive gate: must be atomic */
  _Atomic int ctx_next; /* feed_pkt's per-ctx chain (capture_ts_push_head_ptr) */
  pid_filter_t filter;
  int proto; /* 1=H1, 2=H2, 3=H3 */
  _Atomic int alive;
  _Atomic int ready;
  int fd; /* H1: socket fd. H2: H2 conn fd. H3: -1 */
  int32_t h2_sid;
  int reactor_tid; /* H3: owning reactor thread id */
  int tid_next; /* ts_push_set_reactor_tid()'s per-tid chain. owning thread only */
  void *h3c;
  int64_t h3_sid;
  uint8_t *h3_ring;
  _Atomic uint32_t h3_wpos;
  _Atomic uint32_t h3_rpos;
  uint8_t *h2_ring;
  _Atomic uint32_t h2_wpos;
  _Atomic uint32_t h2_rpos;
  uint8_t *pkt_ring; /* H1 only */
  _Atomic uint32_t pkt_wpos;
  _Atomic uint32_t pkt_rpos;
  _Atomic int pkt_overrun;
  int spts;              /* 1 = /spts: filter to spts_allowed, not just filter's excludes */
  unsigned spts_pmt_pid; /* 0 = auto (first PMT that resolves) */
  psi_t *spts_psi;
  int spts_locked;
  unsigned spts_allowed[PSI_MAX_ES + 3]; /* PAT, locked PMT pid, PCR pid, every ES */
  int spts_n_allowed;
  psi_t *filter_psi; /* non-spts, filter.count>0: tracks PMT for pmt_filter_rewrite(). NULL: none */
  unsigned char cc_pmt; /* rewritten PMT packets' own cc, spts_psi/filter_psi share this: mutually exclusive */
  rawaudio_demux_t *rawaudio; /* non-NULL = /rawaudio subscriber */
  int ws_handle;              /* ws_clients.c registry handle, -1 none */
} ts_sub_t;

extern ts_sub_t *g_ts_subs; /* calloc by ts_push_init, g_ts_subs_n entries */
extern int g_ts_subs_n;

/* prealloc: 0 skips the 32-slot ring prealloc (~36MiB), for when ts/spts/rawaudio are all disabled */
void ts_push_init(int prealloc, int max_clients);

void ts_push_register_reactor_efd(int tid, int efd);
void ts_push_flush_ready(int tid);

/* records idx's tid, links into flush_ready(tid) chain.
   call once, from tid's own thread, when ready to send */
void ts_push_set_reactor_tid(int idx, int tid);

/* allocs slot for ctx, ref freed in ts_push_unsubscribe_by_idx.
   filter: PID excludes.
   spts 1: pmt_pid 0 auto else forced.
   rawaudio 1: /rawaudio demux. spts/rawaudio exclusive.
   -1: room full/OOM */
int ts_push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, int proto, int fd, unsigned pmt_pid, int spts, int rawaudio, const client_info_t *info);

void ts_push_unsubscribe_by_idx(int idx);

/* capture pump thread: fan one TS packet out to every subscriber of ctx */
void ts_push_feed_pkt(capture_ctx_t *ctx, const uint8_t *pkt);

/* count of subscriber slots currently alive, any protocol */
int ts_push_active_count(void);

void h2_tspush_wake(int sub_idx);
void ts_push_h2_enqueue(int sub_idx, const uint8_t *pkt, size_t len);
void ts_push_h3_enqueue(int sub_idx, const uint8_t *pkt, size_t len);

#ifdef HAVE_HTTP3
void ts_push_h3_flush(void);
#endif

#endif
