/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../hls/hls.h"
#include "../../dash/dash.h"
#include "../../segment/mp4push.h"

#include "lib/helper/ioutil.h"

#define LLHLS_WAITERS_MAX 64

static _Thread_local llhls_waiter_t t_llhls_waiters[LLHLS_WAITERS_MAX];
static _Thread_local int t_llhls_waiters_active;

/* 1 parked (caller must not touch c further, epoll left alone). 0 table full: caller serves what's avail */
int llhls_try_park(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename,
                   int is_head, int keep_alive, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle) {
  return llhls_waiter_pool_try_park(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, c, -1, ctx, filter, pmt_pid, filename,
                                    is_head, keep_alive, NULL, origin_hdr, want_seg, want_part, timeout_ms, ws_handle);
}

void llhls_waiter_conn_closing(conn_t *c) {
  llhls_waiter_pool_close_owner(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, c, -1);
}

static void h1_llhls_finish(llhls_waiter_t *w) {
  conn_t *c = w->owner;
  size_t bytes = 0;
  if (hls_serve_ll(c, w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, w->keep_alive, NULL,w->origin[0] ? w->origin : NULL, &bytes))
    ws_clients_add_bytes(w->ws_handle, bytes);
  else
    respond_status(c, RESP_404, w->keep_alive);
  c->state = CONN_WRITING;
  reactor_finish(t_reactor_epfd, c);
}

void llhls_flush_waiters(void) {
  llhls_waiter_pool_flush(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, h1_llhls_finish);
}

#define HLS_COLD_WAITERS_MAX 64

typedef struct {
  conn_t *c;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  char filename[32];
  hls_cold_kind_t kind;
  seg_container_t container; /* HLS_COLD_HLS only, hls vs hls-fmp4 */
  int want_ll;                /* HLS_COLD_DASH only, lldash vs dash */
  int is_head;
  int keep_alive;
  char origin[128]; /* Origin request header, empty if absent */
  int64_t deadline_ms;
  int active;
  int ws_handle;
} hls_cold_waiter_t;

static _Thread_local hls_cold_waiter_t t_hls_cold_waiters[HLS_COLD_WAITERS_MAX];
static _Thread_local int t_hls_cold_waiters_active;

/* 1 parked: manifest requested before capture's first segment/part, waits of an instant unretryable 404.
   0 table full: caller serves now */
int hls_cold_try_park(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename,
                      hls_cold_kind_t kind, seg_container_t container, int want_ll, int is_head, int keep_alive, const char *origin_hdr, int timeout_ms, int ws_handle) {
  for (int i = 0; i < HLS_COLD_WAITERS_MAX; i++) {
    hls_cold_waiter_t *w = &t_hls_cold_waiters[i];
    if (w->active)
      continue;
    w->c = c;
    w->cap_ctx = ctx;
    w->filter = *filter;
    w->pmt_pid = pmt_pid;
    bufcpy(w->filename, sizeof w->filename, filename);
    w->kind = kind;
    w->container = container;
    w->want_ll = want_ll;
    w->is_head = is_head;
    w->keep_alive = keep_alive;
    bufcpy(w->origin, sizeof w->origin, origin_hdr ? origin_hdr : "");
    w->deadline_ms = now_ms() + timeout_ms;
    w->active = 1;
    w->ws_handle = ws_handle;
    t_hls_cold_waiters_active++;
    return 1;
  }
  return 0;
}

void hls_cold_waiter_conn_closing(const conn_t *c) {
  for (int i = 0; i < HLS_COLD_WAITERS_MAX; i++)
    if (t_hls_cold_waiters[i].active && t_hls_cold_waiters[i].c == c) {
      t_hls_cold_waiters[i].active = 0;
      t_hls_cold_waiters_active--;
    }
}

void hls_cold_flush_waiters(void) {
  int64_t now;
  if (!t_hls_cold_waiters_active)
    return;
  now = now_ms();
  for (int i = 0; i < HLS_COLD_WAITERS_MAX; i++) {
    hls_cold_waiter_t *w = &t_hls_cold_waiters[i];
    int ready, served;
    size_t bytes = 0;
    if (!w->active) continue;
    ready = w->kind == HLS_COLD_LLHLS ? hls_ll_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container) : hls_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container);
    if (now < w->deadline_ms && !ready) continue;
    if (w->kind == HLS_COLD_MP4) {
      if (!mp4push_try_attach(w->c, w->cap_ctx, &w->filter, w->pmt_pid, w->ws_handle))
        respond_status(w->c, RESP_501, w->keep_alive);
    } else {
      if (w->kind == HLS_COLD_LLHLS) {
        served = hls_serve_ll(w->c, w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, w->keep_alive, NULL,w->origin[0] ? w->origin : NULL, &bytes);
      } else if (w->kind == HLS_COLD_DASH) {
        served = dash_serve(w->c, w->cap_ctx, &w->filter, w->pmt_pid, w->want_ll, reactor_cfg()->dash_utc_url, w->is_head, w->keep_alive, w->origin[0] ? w->origin : NULL, &bytes);
      } else {
        served = hls_serve(w->c, w->cap_ctx, &w->filter, w->pmt_pid, w->container, w->filename, w->is_head, w->keep_alive, NULL,w->origin[0] ? w->origin : NULL, &bytes);
      }
      if (served) {
        ws_clients_add_bytes(w->ws_handle, bytes);
      } else {
        respond_status(w->c, RESP_404, w->keep_alive);
      }
    }
    w->c->state = CONN_WRITING;
    reactor_finish(t_reactor_epfd, w->c);
    w->active = 0;
    t_hls_cold_waiters_active--;
  }
}
