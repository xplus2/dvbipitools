/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DASH_LLDASH_LLDASH_INT_H
#define DIPIXY_DASH_LLDASH_LLDASH_INT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dipixy/dash/lldash.h"
#include "dipixy/dash/dash_int.h"
#include "dipixy/altsvc.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/version.h"
#include "dipixy/ws/ws_clients.h"
#include "lib/helper/byte_ring.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#define DASHCHUNK_MAX_SUBS 4096
#define DASHCHUNK_SUBS_HEADROOM 16
#define DASHCHUNK_SUBS_FLOOR 32
#define DASHCHUNK_MAX_REACTOR_THREADS REACTOR_MAX_WORKERS
#define DASHCHUNK_RING_BYTES (1u << 23)

#define DASHCHUNK_SUB_FREE 0
#define DASHCHUNK_SUB_ALIVE 1
#define DASHCHUNK_SUB_CLOSING 2

typedef struct {
  _Atomic int alive;
  hls_store_t *store;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  lcevc_select_t lcevc;
  uint32_t want_seg;
  conn_proto_t proto;
  int fd;
  uint64_t conn_gen; /* H1: conn ident, fd may be reused after teardown */
  void *h2c;
  void *h2_slot;
  void *h3c; /* h3: owning h3_conn_t* */
  int64_t h3_sid; /* h3: stream id */
  int ws_handle;
  int reactor_tid; /* h2/h3: owning reactor thread */
  _Atomic int finalized;
  pthread_mutex_t lock; /* serializes delivery: segmenter vs attach */
  int attached; /* consumer ready, parts may flow */
  int sent; /* parts of want_seg delivered */
  int ended; /* terminator/EOF issued */
  int aborted; /* store closed before attach */
  byte_ring_t ring; /* h2/h3 */
  _Atomic int ring_errored;
  _Atomic int store_next;
  int tid_next;
} dashchunk_sub_t;

extern dashchunk_sub_t *g_lldash_subs;
extern int g_lldash_subs_n;
extern int g_lldash_reactor_efds[DASHCHUNK_MAX_REACTOR_THREADS];
extern int g_lldash_tid_head[DASHCHUNK_MAX_REACTOR_THREADS];
int sub_matches(const dashchunk_sub_t *s, const hls_store_t *store, uint32_t seq);
void sub_emit(dashchunk_sub_t *s, const uint8_t *data, size_t len);
void sub_end(dashchunk_sub_t *s);
void sub_abort(dashchunk_sub_t *s);
void sub_attach(int idx);

#endif
