/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_CAPTURE_PRIV_H
#define DIPIXY_CAPTURE_PRIV_H

#include <pthread.h>
#include <stdatomic.h>

#include "lib/fccret/fcc_client.h"
#include "lib/fccret/ret_client.h"
#include "lib/helper/log.h"
#include "lib/net/multicast.h"
#include "lib/net/tssource.h"

#include "capture.h"

#define CAPTURE_RECV_BUF 65536

typedef enum { CAP_BACKEND_MCAST, CAP_BACKEND_TSSRC } cap_backend_t;

struct capture_ctx {
  cap_backend_t backend;
  int family;
  char group[64];
  unsigned port;
  char *iface; /* malloc'd, NULL = kernel default */
  int rtp;
  mcast_t *m;
  ret_client_t *ret;
  fcc_client_t *fcc;

  /* rist/srt/stdin/http share these fields */
  tssrc_t *ts;
  char *key; /* SRT dedup key, NULL otherwise */
  unsigned char leftover[188]; /* partial packet tail, see tssrc_drain() */
  size_t leftover_len;

  _Atomic int refcount;

  unsigned char *ring; /* CAP_BACKEND_MCAST only, capture_service()/capture_reader_* */
  _Atomic uint64_t write_total; /* monotonic bytes ever written */

  _Atomic int ts_push_head; /* ts_push.c's per-ctx subscriber chain, -1: none */
  _Atomic(void *) hls_seg_head;

  int pump_shard; /* owning pump thread, set once at creation, read-only after */

  log_throttle_t drain_err_throttle;

  struct capture_ctx *next;
};

struct capture_reader {
  capture_ctx_t *ctx;
  uint64_t read_total;
  uint64_t dropped;
  log_throttle_t drop_throttle;
};

typedef struct capture_snapshot {
  struct capture_snapshot *retired_next; /* g_retired list, g_lock protected */
  int n;
  capture_ctx_t *ctxs[];
} capture_snapshot_t;

/* capture.c */
extern size_t g_capture_ring_cap; /* capture_set_ring_cap()'d, default 4 MiB */
extern capture_ctx_t *g_open;
extern pthread_mutex_t g_lock;
extern _Atomic(capture_snapshot_t *) g_snapshot;
extern capture_snapshot_t *g_retired; /* superseded snapshots pending reclaim, g_lock protected */
capture_ctx_t *find_existing_tssrc(const char *key);
void rebuild_snapshot(void); /* caller holds g_lock */
void unlink_ctx(capture_ctx_t *ctx);
void free_ctx_resources(capture_ctx_t *ctx);
void reclaim_retired_snapshots(void); /* caller holds no lock, not on a pump thread */

/* pump.c */
int next_pump_shard(void);

#endif
