/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../dash/dash.h"
#include "../reactor/internal.h"
#include "../httpng/httpng.h"
#include "http3.h"
#include "http3_int.h"

#include "lib/helper/ioutil.h"

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
      h3_respond_status(conn, w->stream_id, "501");
    }
  } else {
    handled = httpng_hls_cold_render(w->kind, w->container, w->want_ll, w->cap_ctx, &w->filter, w->pmt_pid, &w->lcevc, w->filename, w->is_head, &resp);
    if (!handled) {
      h3_respond_status(conn, w->stream_id, "404");
    } else {
      h3_submit_resp(conn, r, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.zc, w->origin[0] ? w->origin : NULL);
      if (resp.status == 200) ws_clients_add_bytes(w->ws_handle, resp.body_len);
    }
  }
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0) flush_tx(conn, fd);
}

void h3_hls_cold_flush_waiters(void) {
  llhls_waiter_pool_flush(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, hls_cold_ready, h3_hls_cold_finish);
}

#endif /* HAVE_HTTP3 */
