/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../dash/dash.h"
#include "../reactor/internal.h"
#include "http3.h"
#include "http3_int.h"

#include "lib/helper/ioutil.h"

#define H3_HLS_COLD_WAITERS_MAX 64

static _Thread_local llhls_waiter_t t_h3_hls_cold_waiters[H3_HLS_COLD_WAITERS_MAX];
static _Thread_local int t_h3_hls_cold_waiters_active;

int h3_hls_cold_try_park(h3_conn_t *conn, int64_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, hls_cold_kind_t kind,
                         seg_container_t container, int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle) {
  return hls_cold_waiter_pool_try_park(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, conn, stream_id, ctx, filter, pmt_pid, filename,
                                       kind, container, want_ll, is_head, 0, origin_hdr, timeout_ms, ws_handle);
}

void h3_hls_cold_on_stream_close(const h3_conn_t *c, int64_t stream_id) {
  llhls_waiter_pool_close_owner(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, (void *)c, stream_id);
}

void h3_hls_cold_on_conn_close(const h3_conn_t *c) {
  llhls_waiter_pool_close_owner(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, (void *)c, -1);
}

static void h3_hls_cold_finish(llhls_waiter_t *w) {
  h3_conn_t *conn = w->owner;
  h3_req_t *r;
  int handled, fd;
  hls_resp_t resp;

  r = find_req(conn, w->stream_id);
  if (!r) return;
  if (w->kind == HLS_COLD_MP4) {
    int sub = mp4push_subscribe(w->cap_ctx, &w->filter, w->pmt_pid, 3);
    if (sub >= 0) {
      nghttp3_nv nva[] = {
          {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE},
          {(uint8_t *)"content-type", (uint8_t *)"video/mp4", 12, 9, NGHTTP3_NV_FLAG_NONE},
      };
      nghttp3_data_reader dr;
      dr.read_data = h3_mp4push_read_cb;
      r->mp4push_sub_idx = sub;
      mp4push_h3_bind(sub, conn, w->stream_id, t_reactor_tid, w->ws_handle);
      nghttp3_conn_set_stream_user_data(conn->h3conn, w->stream_id, r);
      nghttp3_conn_submit_response(conn->h3conn, w->stream_id, nva, 2, &dr);
    } else {
      h3_respond_status(conn, w->stream_id, "501");
    }
  } else {
    if (w->kind == HLS_COLD_LLHLS)
      handled = hls_render_ll(w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, NULL, &resp);
    else if (w->kind == HLS_COLD_DASH)
      handled = dash_render(w->cap_ctx, &w->filter, w->pmt_pid, w->want_ll, reactor_cfg()->dash_utc_url, w->is_head, &resp);
    else
      handled = hls_render(w->cap_ctx, &w->filter, w->pmt_pid, w->container, w->filename, w->is_head, NULL, &resp);
    if (!handled) {
      h3_respond_status(conn, w->stream_id, "404");
    } else {
      h3_submit_resp(conn, r, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.zc, w->origin[0] ? w->origin : NULL);
      if (resp.status == 200)
        ws_clients_add_bytes(w->ws_handle, resp.body_len);
    }
  }
  fd = conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd >= 0) flush_tx(conn, fd);
}

void h3_hls_cold_flush_waiters(void) {
  llhls_waiter_pool_flush(t_h3_hls_cold_waiters, H3_HLS_COLD_WAITERS_MAX, &t_h3_hls_cold_waiters_active, hls_cold_ready, h3_hls_cold_finish);
}

#endif /* HAVE_HTTP3 */
