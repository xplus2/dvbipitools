/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* LL-DASH progressive segment delivery: streams a still-muxing FMP4 segment's
   CMAF chunks to a client as they're produced. H1: HTTP/1.1 chunked
   transfer-encoding. H2/H3: per-stream ring drained by a DATA read callback,
   EOF on finalize. */

#ifndef DIPIXY_DASH_LLDASH_H
#define DIPIXY_DASH_LLDASH_H

#include "../segstore.h"
#include "../reactor/conn.h"

#include <stdint.h>

typedef enum { DASH_PROTO_H1 = 1, DASH_PROTO_H2 = 2, DASH_PROTO_H3 = 3 } dash_proto_t;

/* call once at startup, before any traffic */
void dash_lldash_init(int max_clients);

/* c: headers not yet queued.1 attached (caller must not touch c), 0 no store/filename: 404 */
int dash_lldash_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int keep_alive, const char *origin_hdr, int ws_handle);

/* protocol-agnostic. -1: not applicable/full, caller falls to normal render path */
int dash_lldash_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, dash_proto_t proto);

void dash_lldash_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle);

void *dash_lldash_sub_h2c(int slot);
void *dash_lldash_sub_h2_slot(int slot);

void dash_lldash_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle);

/* h3: slot's bound h3_conn_t + stream id, NULL/-1 if unbound/out of range */
void *dash_lldash_sub_h3c(int slot);
int64_t dash_lldash_sub_h3_sid(int slot);

/* h2: drains up to maxlen bytes into buf, returns bytes copied (0: ring empty) */
size_t dash_lldash_ring_read(int slot, uint8_t *buf, size_t maxlen);

/* h3 zero-copy read: peek then advance */
const uint8_t *dash_lldash_ring_peek(int slot, size_t *len);
void dash_lldash_ring_advance(int slot, size_t n);

/* h2/h3: 1 if ring has bytes still to drain */
int dash_lldash_ring_pending(int slot);

/* h2/h3: 1 if a ring enqueue overflowed (stream should be aborted, not just finalized) */
int dash_lldash_ring_errored(int slot);

/* reactor_dashchunk.c/http2_dashchunk.c/http3_dashchunk.c: 1 once the
   subscribed segment has finalized (terminating chunk queued/EOF eligible) */
int dash_lldash_sub_finalized(int slot);

/* reactor_dashchunk.c/http2_dashchunk.c/http3_dashchunk.c: releases slot */
void dash_lldash_sub_close(int slot);

/* reactor_listen.c: per-reactor-thread wakeup eventfd for h2/h3 chunk. -1 efd: unregister */
void dash_lldash_register_reactor_efd(int tid, int efd);

/* reactor_loop.c: called on that eventfd's readable event, wakes pending h2/h3 subs owned by tid */
void dash_lldash_flush_ready(int tid);

/* http2_dashchunk.c: resumes a deferred H2 DATA read for sub_idx, must run on its owning reactor thread */
void h2_dashchunk_wake(int sub_idx);

/* http3_dashchunk.c: resumes a deferred H3 DATA read for sub_idx, must run on its owning reactor thread */
void h3_dashchunk_wake(int sub_idx);

#endif
