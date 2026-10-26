/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../hls/hls.h"
#include "../../dash/dash.h"
#include "../../segment/mp4push.h"
#include "../../ts/ts_push.h"

#define LLHLS_WAITERS_MAX 64

static _Thread_local hls_waiter_t t_llhls_waiters[LLHLS_WAITERS_MAX];
static _Thread_local int t_llhls_waiters_active;

/* 1 parked (caller must not touch c further, epoll left alone). 0 table full: caller serves what's avail */
int llhls_try_park(conn_t *c, const llhls_park_req_t *req) {
  return llhls_waiter_pool_try_park(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, c, -1, req);
}

void llhls_waiter_conn_closing(const conn_t *c) {
  llhls_waiter_pool_close_owner(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, c, -1);
}

static void h1_llhls_finish(hls_waiter_t *w) {
  conn_t *c = w->owner;
  size_t bytes = 0;
  if (hls_serve_ll(c, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->filename, w->is_head, w->keep_alive, NULL,w->origin[0] ? w->origin : NULL, &bytes))
    ws_clients_add_bytes(w->ws_handle, bytes);
  else
    respond_status(c, RESP_404, w->keep_alive);
  c->state = CONN_WRITING;
  reactor_finish(t_reactor_epfd, c);
}

void llhls_flush_waiters(void) {
  llhls_waiter_pool_flush(t_llhls_waiters, LLHLS_WAITERS_MAX, &t_llhls_waiters_active, llhls_ready_part, h1_llhls_finish);
}

#define HLS_COLD_WAITERS_MAX 64

static _Thread_local hls_waiter_t t_hls_cold_waiters[HLS_COLD_WAITERS_MAX];
static _Thread_local int t_hls_cold_waiters_active;

/* 1 parked: manifest requested before capture's first segment/part, waits of an instant unretryable 404.
   0 table full: caller serves now */
int hls_cold_try_park(conn_t *c, const hls_cold_park_req_t *req) {
  return hls_cold_waiter_pool_try_park(t_hls_cold_waiters, HLS_COLD_WAITERS_MAX, &t_hls_cold_waiters_active, c, -1, req);
}

void hls_cold_waiter_conn_closing(const conn_t *c) {
  llhls_waiter_pool_close_owner(t_hls_cold_waiters, HLS_COLD_WAITERS_MAX, &t_hls_cold_waiters_active, c, -1);
}

static void hls_cold_finish(hls_waiter_t *w) {
  conn_t *c = w->owner;
  int served;
  size_t bytes = 0;
  if (w->kind != HLS_COLD_MP4 && !hls_cold_ready(w)) {
    respond_status(c, hls_cold_expired_status(w) == 503 ? RESP_503 : RESP_404, w->keep_alive);
    c->state = CONN_WRITING;
    reactor_finish(t_reactor_epfd, c);
    return;
  }
  switch (w->kind) {
    case HLS_COLD_MP4:
      if (!mp4push_try_attach(c, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->ws_handle)) respond_status(c, RESP_503, w->keep_alive);
      break;
    case HLS_COLD_LLHLS:
      served = hls_serve_ll(c, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->filename, w->is_head, w->keep_alive, NULL, w->origin[0] ? w->origin : NULL, &bytes);
      if (served) ws_clients_add_bytes(w->ws_handle, bytes); else respond_status(c, RESP_404, w->keep_alive);
      break;
    case HLS_COLD_DASH:
      served = dash_serve(c, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->want_ll, w->utc_url, w->is_head, w->keep_alive, w->origin[0] ? w->origin : NULL, &bytes);
      if (served) ws_clients_add_bytes(w->ws_handle, bytes); else respond_status(c, RESP_404, w->keep_alive);
      break;
    case HLS_COLD_HLS:
      served = hls_serve(c, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->container, w->filename, w->is_head, w->keep_alive, NULL, w->origin[0] ? w->origin : NULL, &bytes);
      if (served) ws_clients_add_bytes(w->ws_handle, bytes); else respond_status(c, RESP_404, w->keep_alive);
      break;
  }
  c->state = CONN_WRITING;
  reactor_finish(t_reactor_epfd, c);
}

void hls_cold_flush_waiters(void) {
  llhls_waiter_pool_flush(t_hls_cold_waiters, HLS_COLD_WAITERS_MAX, &t_hls_cold_waiters_active, hls_cold_ready, hls_cold_finish);
}

#define OPEN_WAITERS_MAX 64

static _Thread_local open_waiter_t t_open_waiters[OPEN_WAITERS_MAX];
static _Thread_local int t_open_waiters_active;

int open_try_park(conn_t *c, capture_open_job_t *job) {
  return open_waiter_pool_try_park(t_open_waiters, OPEN_WAITERS_MAX, &t_open_waiters_active, c, -1, job);
}

void open_waiter_conn_closing(const conn_t *c) {
  open_waiter_pool_close_owner(t_open_waiters, OPEN_WAITERS_MAX, &t_open_waiters_active, c, -1);
}

static void h1_open_resume(open_waiter_t *w, capture_ctx_t *ctx) {
  conn_t *c = w->owner;
  open_replay_begin(ctx);
  c->state = CONN_READING;
  reactor_read(t_reactor_epfd, c);
  open_replay_end(ctx);
}

void open_flush_waiters(void) {
  open_waiter_pool_flush(t_open_waiters, OPEN_WAITERS_MAX, &t_open_waiters_active, h1_open_resume);
}

#define TS_COLD_WAITERS_MAX 64

static _Thread_local ts_cold_waiter_t t_ts_cold_waiters[TS_COLD_WAITERS_MAX];
static _Thread_local int t_ts_cold_waiters_active;

/* 1 parked: joined, no traffic (yet), ts-startup-timeout/504. 0 table full: caller serves */
int ts_cold_try_park(conn_t *c, const ts_cold_park_req_t *req) {
  return ts_cold_waiter_pool_try_park(t_ts_cold_waiters, TS_COLD_WAITERS_MAX, &t_ts_cold_waiters_active, c, -1, req);
}

/* client gone before ready/timeout: releases waiter's cap_ctx ref */
void ts_cold_waiter_conn_closing(const conn_t *c) {
  ts_cold_waiter_pool_close_owner(t_ts_cold_waiters, TS_COLD_WAITERS_MAX, &t_ts_cold_waiters_active, c, -1);
}

static void ts_cold_finish(ts_cold_waiter_t *w) {
  conn_t *c = w->owner;
  if (!capture_ctx_bytes(w->cap_ctx)) {
    capture_close(w->cap_ctx);
    respond_status(c, RESP_504, w->keep_alive);
  } else {
    char header[160];
    size_t header_len = build_stream_header(header, sizeof header, w->rawaudio ? "audio/mpeg" : "video/mp2t", c->ssl != NULL);
    client_info_t cinfo;
    route_item_bufs_t item_bufs;
    int sub;
    route_client_info(&w->rt, w->list_num, &w->filter, w->tp_pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
    sub = ts_push_subscribe(w->cap_ctx, &w->filter, CONN_PROTO_H1, c->fd, w->tp_pmt_pid, w->spts, w->rawaudio, &cinfo, &w->lcevc);
    if (sub < 0) {
      capture_close(w->cap_ctx);
      respond_status(c, RESP_503, w->keep_alive);
    } else {
      c->slot = sub;
      conn_queue(c, header, header_len);
      c->next_state = CONN_NEXT_TSPUSH;
    }
  }
  c->state = CONN_WRITING;
  reactor_finish(t_reactor_epfd, c);
}

void ts_cold_flush_waiters(void) {
  ts_cold_waiter_pool_flush(t_ts_cold_waiters, TS_COLD_WAITERS_MAX, &t_ts_cold_waiters_active, ts_cold_finish);
}
