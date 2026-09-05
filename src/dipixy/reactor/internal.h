/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* private glue between src/dipixy/ reactor files */

#ifndef DIPIXY_INTERNAL_H
#define DIPIXY_INTERNAL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/ioutil.h"
#include "lib/metrics/export.h"

#include "../args.h"
#include "../ts/capture/capture.h"
#include "../ts/channels/channels.h"
#include "../hls/hls.h"
#include "../core/route.h"
#include "../ws/ws_clients.h"
#include "conn.h"
#include "lib/vendor/picohttpparser/picohttpparser.h"

extern long g_connections_total;
extern _Thread_local int t_reactor_tid;
extern _Thread_local int t_reactor_epfd;

void reactor_conn_flush(int epfd, conn_t *c);

/* cfg/channels handed to reactor_run(), read-only for its lifetime */
const config_t *reactor_cfg(void);
const channels_t *reactor_channels(void);

/* re-reads every source right now, same as channels_reload_all() from a SIGHUP */
void reactor_reload_channels(void);

typedef enum {
  RL_ACCEPT = 0,     /* TCP/TLS accept socket */
  RL_TSPUSH_EFD,     /* per-reactor TS-push wakeup eventfd */
  RL_DASHCHUNK_EFD,  /* per-reactor LL-DASH chunk wakeup eventfd (H2/H3) */
  RL_MP4PUSH_EFD,    /* per-reactor progressive-MP4 wakeup eventfd (H2/H3) */
  RL_H3_UDP          /* QUIC UDP socket (HAVE_HTTP3 only) */
} reactor_listener_kind;

typedef struct {
  int fd;
  int is_tls;
  reactor_listener_kind kind;
} reactor_listener;

typedef struct {
  reactor_listener L[8]; /* 2 TCP (plain+tls), 2 H3 UDP (v4+v6), 1 tspush efd, 1 dashchunk efd, 1 mp4push efd */
  int nL;
  int tspush_efd;
  int dashchunk_efd;
  int mp4push_efd;
} reactor_listeners_t;

/* reactor.c: conn lifecycle */
void reactor_arm(int epfd, conn_t *c, int want_out);
void reactor_finish(int epfd, conn_t *c);
void reactor_close(int epfd, conn_t *c);

/* reactor.c: metrics exporter handed to reactor_run(), read-only for its lifetime */
metrics_exporter_t *reactor_metrics(void);

/* reactor.c. tid-0 worker calls this once its listeners are up: log listening ln + trigger on_listening callback */
void reactor_notify_listening(void);

/* reactor_listen.c */
void reactor_setup_listeners(reactor_listeners_t *rl, int epfd, int tid);
void reactor_teardown_listeners(const reactor_listeners_t *rl, int tid);
void reactor_raise_nofile_limit(void);

/* reactor_loop.c: worker/pump thread entry points, spawned via pthread_create() from reactor_run() */
void *worker_thread(void *arg);
void *pump_thread(void *arg);

/* dispatch.c: HTTP/1.1 request read + dispatch */
void reactor_read(int epfd, conn_t *c);
void reactor_keepalive(int epfd, conn_t *c);

/* dispatch.c, shared with http2.c/http3_req.c */
int route_disabled(const route_t *rt);
/* out_list_num: resolved list_num for ROUTE_LIST_ITEM/NAME (direct or via src_name), NULL ok */
capture_ctx_t *open_source(const route_t *rt, unsigned *out_list_num);

/* caller's stack storage for route_client_info()'s out->item_name/src_proto/src_addr */
typedef struct {
  char name[128];
  char proto[16];
  char addr[80]; /* rt->addr (63) + ':' + port (5) + nul */
} route_item_bufs_t;

/* fills *out from rt (post open_source, list_num = its out_list_num) */
void route_client_info(const route_t *rt, unsigned list_num, const pid_filter_t *filter, unsigned pmt_pid, const char *client_ip, int http_ver, route_item_bufs_t *bufs, client_info_t *out);
void strip_etag_quotes(char *v);
int find_header(const struct phr_header *headers, size_t num_headers, const char *name, char *out, size_t outsz);

/* origin_hdr vs cors_origins allowlist ("*" ok). unset: always "*". match sets *vary, else NULL */
const char *cors_match(const config_t *cfg, const char *origin_hdr, int *vary);

/* Authorization header value vs cfg->http_auth. 1: auth disabled or matches, 0: reject */
int http_auth_ok(const config_t *cfg, const char *auth_hdr);

/* dispatch.c. runs each worker loop iteration, resumes CONN_DISPATCH-parked
   LL-HLS blocking-reload waits */
void llhls_flush_waiters(void);

/* dispatch.c. purges c's waiter slot, call before reactor_close() frees it */
void llhls_waiter_conn_closing(conn_t *c);

/* clock_gettime(CLOCK_MONOTONIC) in ms, shared by dispatch.c/http2_hls.c/http3_req.c */
static inline int64_t now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* parses _HLS_msn=<N>&_HLS_part=<P> from a query string. 1 if both present */
static inline int parse_blocking_reload(const char *query, uint32_t *want_seg, int *want_part) {
  const char *msn = query ? strstr(query, "_HLS_msn=") : NULL;
  const char *part = query ? strstr(query, "_HLS_part=") : NULL;
  if (!msn || !part) return 0;
  *want_seg = (uint32_t)strtoul(msn + 9, NULL, 10);
  *want_part = (int)strtol(part + 10, NULL, 10);
  return 1;
}

/* generic LL-HLS blocking-reload waiter pool. owner+stream_id: protocol conn/stream (stream_id -1: no stream concept, h1).
   finish_fn: protocol-specific respond+flush, called after slot released */
typedef struct {
  void *owner;
  int64_t stream_id;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  char filename[32];
  int is_head;
  int keep_alive; /* h1 only, unused by h2/h3 */
  char inm[80];    /* h2/h3 only, empty for h1 */
  char origin[128];
  uint32_t want_seg;
  int want_part;
  int64_t deadline_ms;
  int active;
  int ws_handle;
} llhls_waiter_t;

typedef void (*llhls_waiter_finish_fn)(llhls_waiter_t *w);

/* 1 parked (caller stops touching conn/stream), 0 table full */
static inline int llhls_waiter_pool_try_park(llhls_waiter_t *slots, int cap, int *active_count, void *owner,  int64_t stream_id, capture_ctx_t *cap_ctx, const pid_filter_t *filter,
                                             unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *inm, const char *origin_hdr, uint32_t want_seg, int want_part,
                                             int timeout_ms, int ws_handle) {
  for (int i = 0; i < cap; i++) {
    llhls_waiter_t *w = &slots[i];
    if (w->active)
      continue;
    w->owner = owner;
    w->stream_id = stream_id;
    w->cap_ctx = cap_ctx;
    w->filter = *filter;
    w->pmt_pid = pmt_pid;
    bufcpy(w->filename, sizeof w->filename, filename);
    w->is_head = is_head;
    w->keep_alive = keep_alive;
    bufcpy(w->inm, sizeof w->inm, inm ? inm : "");
    bufcpy(w->origin, sizeof w->origin, origin_hdr ? origin_hdr : "");
    w->want_seg = want_seg;
    w->want_part = want_part;
    w->deadline_ms = now_ms() + timeout_ms;
    w->active = 1;
    w->ws_handle = ws_handle;
    (*active_count)++;
    return 1;
  }
  return 0;
}

/* stream_id < 0: match owner only (conn closing). else: match owner + exact stream (stream closing) */
static inline void llhls_waiter_pool_close_owner(llhls_waiter_t *slots, int cap, int *active_count, void *owner, int64_t stream_id) {
  for (int i = 0; i < cap; i++) if (slots[i].active && slots[i].owner == owner && (stream_id < 0 || slots[i].stream_id == stream_id)) {
    slots[i].active = 0;
    (*active_count)--;
  }
}

static inline void llhls_waiter_pool_flush(llhls_waiter_t *slots, int cap, int *active_count, llhls_waiter_finish_fn finish) {
  int64_t now;
  if (*active_count <= 0) return;
  now = now_ms();
  for (int i = 0; i < cap; i++) {
    llhls_waiter_t *w = &slots[i];
    if (!w->active) continue;
    if (now < w->deadline_ms && !hls_part_available(w->cap_ctx, &w->filter, w->pmt_pid, SEG_CONTAINER_TS, w->want_seg, w->want_part)) continue;
    w->active = 0;
    (*active_count)--;
    finish(w);
  }
}

/* dispatch.c. runs each worker loop iteration, resumes CONN_DISPATCH-parked plain HLS/hls-fmp4 index.m3u8 cold-start waits */
void hls_cold_flush_waiters(void);

/* dispatch.c. purges c's waiter slot, call before reactor_close() frees it */
void hls_cold_waiter_conn_closing(const conn_t *c);

/* index.m3u8/index_ll.m3u8/manifest.mpd/mp4 requested b4 first segment/part exists */
typedef enum { HLS_COLD_HLS, HLS_COLD_LLHLS, HLS_COLD_DASH, HLS_COLD_MP4 } hls_cold_kind_t;

/* handshake.c: TLS handshake + accept */
void reactor_handshake(int epfd, conn_t *c);
void reactor_accept(int epfd, reactor_listener *L);

/* reactor_tspush.c */
void reactor_tspush_begin(int epfd, conn_t *c);
void reactor_tspush_readable(int epfd, conn_t *c);
void reactor_tspush_close(int epfd, conn_t *c);

/* reactor_dashchunk.c */
void reactor_dashchunk_begin(int epfd, conn_t *c);
void reactor_dashchunk_readable(int epfd, conn_t *c);
void reactor_dashchunk_flush(int epfd, conn_t *c);
void reactor_dashchunk_close(int epfd, conn_t *c);

/* reactor_mp4push.c */
void reactor_mp4push_begin(int epfd, conn_t *c);
void reactor_mp4push_readable(int epfd, conn_t *c);
void reactor_mp4push_flush(int epfd, conn_t *c);
void reactor_mp4push_close(int epfd, conn_t *c);

/* CONN_WS lifecycle. dispatch.c queues 101, sets become_ws */
void reactor_ws_begin(int epfd, conn_t *c);
void reactor_ws_readable(int epfd, conn_t *c);
void reactor_ws_close(int epfd, conn_t *c);
void reactor_ws_flush(int epfd, conn_t *c);

/* 1: handled (upgraded or rejected), 0: not WS */
int ws_try_upgrade(conn_t *c, const char *path, const struct phr_header *headers, size_t num_headers, int keep_alive);

#endif
