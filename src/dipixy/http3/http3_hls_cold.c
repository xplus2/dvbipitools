/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../dash/dash.h"
#include "../ts/ts_push.h"
#include "../reactor/internal.h"
#include "../httpng/httpng.h"
#include "http3.h"
#include "http3_int.h"

#include "lib/sys/ioutil.h"

#define H3_HLS_COLD_WAITERS_MAX 64

static _Thread_local hls_waiter_t t_h3_hls_cold_waiters[H3_HLS_COLD_WAITERS_MAX];
static _Thread_local int t_h3_hls_cold_waiters_active;

int h3_hls_cold_try_park(h3_conn_t *conn, int64_t stream_id, const hls_cold_park_req_t *req) {
  return hls_cold_waiter_pool_try_park(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, conn, stream_id, req);
}

void h3_hls_cold_on_stream_close(const h3_conn_t *c, int64_t stream_id) {
  llhls_waiter_pool_close_owner(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, c, stream_id);
}

void h3_hls_cold_on_conn_close(const h3_conn_t *c) {
  llhls_waiter_pool_close_owner(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, c, -1);
}

static void h3_hls_cold_finish(hls_waiter_t *w) {
  h3_conn_t *conn = w->owner;
  h3_req_t *r;
  int handled;
  int fd;
  hls_resp_t resp;

  r = find_req(conn, w->stream_id);
  if (!r) return;
  if (w->kind == HLS_COLD_MP4) {
    int sub = mp4push_subscribe(w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, CONN_PROTO_H3);
    if (sub < 0 || !h3_mp4push_dispatch(conn, r, sub, w->ws_handle)) {
      if (sub >= 0) mp4push_sub_close(sub);
      h3_respond_status(conn, w->stream_id, "503");
    }
  } else if (!hls_cold_ready(w)) {
    h3_respond_status(conn, w->stream_id, hls_cold_expired_status(w) == 503 ? "503" : "404");
  } else {
    handled = httpng_hls_cold_render(w->kind, w->container, w->want_ll, w->utc_url, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->filename, w->is_head, &resp);
    if (!handled) {
      h3_respond_status(conn, w->stream_id, "404");
    } else {
      h3_submit_resp(conn, r, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.owner, resp.zc, w->origin[0] ? w->origin : NULL);
      if (resp.status == 200) ws_clients_add_bytes(w->ws_handle, resp.body_len);
    }
  }
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0) flush_tx(conn, fd);
}

void h3_hls_cold_flush_waiters(void) {
  llhls_waiter_pool_flush(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, hls_cold_ready, h3_hls_cold_finish);
}

#define H3_TS_COLD_WAITERS_MAX 64

static _Thread_local ts_cold_waiter_t t_h3_ts_cold_waiters[H3_TS_COLD_WAITERS_MAX];
static _Thread_local int t_h3_ts_cold_waiters_active;

/* 1 parked: no packet yet, waits for first one or ts-startup-timeout. 0 table full: caller serves */
int h3_ts_cold_try_park(h3_conn_t *conn, int64_t stream_id, const ts_cold_park_req_t *req) {
  return ts_cold_waiter_pool_try_park(t_h3_ts_cold_waiters, H3_TS_COLD_WAITERS_MAX, &t_h3_ts_cold_waiters_active, conn, stream_id, req);
}

void h3_ts_cold_on_stream_close(const h3_conn_t *c, int64_t stream_id) {
  ts_cold_waiter_pool_close_owner(t_h3_ts_cold_waiters, H3_TS_COLD_WAITERS_MAX, &t_h3_ts_cold_waiters_active, c, stream_id);
}

void h3_ts_cold_on_conn_close(const h3_conn_t *c) {
  ts_cold_waiter_pool_close_owner(t_h3_ts_cold_waiters, H3_TS_COLD_WAITERS_MAX, &t_h3_ts_cold_waiters_active, c, -1);
}

static void h3_ts_cold_finish(ts_cold_waiter_t *w) {
  h3_conn_t *conn = w->owner;
  h3_req_t *r = find_req(conn, w->stream_id);
  char client_ip[64];
  const char *err = NULL;
  int sub;
  int fd;
  if (!r) {
    capture_close(w->cap_ctx);
    return;
  }
  h3_client_ip(conn, client_ip, sizeof client_ip);
  if (!httpng_ts_cold_subscribe(w, CONN_PROTO_H3, -1, client_ip, &sub, &err)) {
    h3_respond_status(conn, w->stream_id, err);
  } else if (!h3_tspush_dispatch(conn, r, sub, w->rawaudio)) {
    ts_push_unsubscribe_by_idx(sub);
    h3_respond_status(conn, w->stream_id, "503");
  }
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0) flush_tx(conn, fd);
}

void h3_ts_cold_flush_waiters(void) {
  ts_cold_waiter_pool_flush(t_h3_ts_cold_waiters, H3_TS_COLD_WAITERS_MAX, &t_h3_ts_cold_waiters_active, h3_ts_cold_finish);
}

#define H3_OPEN_WAITERS_MAX 64

static _Thread_local open_waiter_t t_h3_open_waiters[H3_OPEN_WAITERS_MAX];
static _Thread_local int t_h3_open_waiters_active;

int h3_open_try_park(h3_conn_t *conn, int64_t stream_id, capture_open_job_t *job) {
  return open_waiter_pool_try_park(t_h3_open_waiters, H3_OPEN_WAITERS_MAX, &t_h3_open_waiters_active, conn, stream_id, job);
}

void h3_open_on_stream_close(const h3_conn_t *c, int64_t stream_id) {
  open_waiter_pool_close_owner(t_h3_open_waiters, H3_OPEN_WAITERS_MAX, &t_h3_open_waiters_active, c, stream_id);
}

void h3_open_on_conn_close(const h3_conn_t *c) {
  open_waiter_pool_close_owner(t_h3_open_waiters, H3_OPEN_WAITERS_MAX, &t_h3_open_waiters_active, c, -1);
}

static void h3_open_resume(open_waiter_t *w, capture_ctx_t *ctx) {
  h3_conn_t *conn = w->owner;
  h3_req_t *r = find_req(conn, w->stream_id);
  int fd;
  if (!r) {
    capture_close(ctx);
    return;
  }
  open_replay_begin(ctx);
  r->dispatched = 0;
  dispatch_req(conn, r);
  open_replay_end(ctx);
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0) flush_tx(conn, fd);
}

void h3_open_flush_waiters(void) {
  open_waiter_pool_flush(t_h3_open_waiters, H3_OPEN_WAITERS_MAX, &t_h3_open_waiters_active, h3_open_resume);
}

#endif /* HAVE_HTTP3 */
