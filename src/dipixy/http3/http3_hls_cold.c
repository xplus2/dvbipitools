/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../dash/dash.h"
#include "../reactor/internal.h"
#include "http3.h"
#include "http3_int.h"

#include "lib/helper/ioutil.h"

#define H3_HLS_COLD_WAITERS_MAX 64

typedef struct {
  h3_conn_t *conn;
  int64_t stream_id;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  char filename[32];
  hls_cold_kind_t kind;
  seg_container_t container;
  int want_ll;
  int is_head;
  char origin[128];
  int64_t deadline_ms;
  int active;
  int ws_handle;
} h3_hls_cold_waiter_t;

static _Thread_local h3_hls_cold_waiter_t t_h3_hls_cold_waiters[H3_HLS_COLD_WAITERS_MAX];
static _Thread_local int t_h3_hls_cold_waiters_active;

int h3_hls_cold_try_park(h3_conn_t *conn, int64_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, hls_cold_kind_t kind,
                         seg_container_t container, int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle) {
  for (int i = 0; i < H3_HLS_COLD_WAITERS_MAX; i++) {
    h3_hls_cold_waiter_t *w = &t_h3_hls_cold_waiters[i];
    if (w->active)
      continue;
    w->conn = conn;
    w->stream_id = stream_id;
    w->cap_ctx = ctx;
    w->filter = *filter;
    w->pmt_pid = pmt_pid;
    bufcpy(w->filename, sizeof w->filename, filename);
    w->kind = kind;
    w->container = container;
    w->want_ll = want_ll;
    w->is_head = is_head;
    bufcpy(w->origin, sizeof w->origin, origin_hdr ? origin_hdr : "");
    w->deadline_ms = now_ms() + timeout_ms;
    w->active = 1;
    w->ws_handle = ws_handle;
    t_h3_hls_cold_waiters_active++;
    return 1;
  }
  return 0;
}

void h3_hls_cold_on_stream_close(const h3_conn_t *c, int64_t stream_id) {
  for (int i = 0; i < H3_HLS_COLD_WAITERS_MAX; i++)
    if (t_h3_hls_cold_waiters[i].active && t_h3_hls_cold_waiters[i].conn == c && t_h3_hls_cold_waiters[i].stream_id == stream_id) {
      t_h3_hls_cold_waiters[i].active = 0;
      t_h3_hls_cold_waiters_active--;
    }
}

void h3_hls_cold_on_conn_close(const h3_conn_t *c) {
  for (int i = 0; i < H3_HLS_COLD_WAITERS_MAX; i++)
    if (t_h3_hls_cold_waiters[i].active && t_h3_hls_cold_waiters[i].conn == c) {
      t_h3_hls_cold_waiters[i].active = 0;
      t_h3_hls_cold_waiters_active--;
    }
}

void h3_hls_cold_flush_waiters(void) {
  int64_t now;
  if (!t_h3_hls_cold_waiters_active)
    return;
  now = now_ms();
  for (int i = 0; i < H3_HLS_COLD_WAITERS_MAX; i++) {
    h3_hls_cold_waiter_t *w = &t_h3_hls_cold_waiters[i];
    h3_req_t *r;
    int ready, handled, fd;
    hls_resp_t resp;
    if (!w->active) continue;
    ready = w->kind == HLS_COLD_LLHLS ? hls_ll_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container) : hls_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container);
    if (now < w->deadline_ms && !ready) continue;
    w->active = 0;
    t_h3_hls_cold_waiters_active--;
    r = find_req(w->conn, w->stream_id);
    if (!r) continue;
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
        mp4push_h3_bind(sub, w->conn, w->stream_id, t_reactor_tid, w->ws_handle);
        nghttp3_conn_set_stream_user_data(w->conn->h3conn, w->stream_id, r);
        nghttp3_conn_submit_response(w->conn->h3conn, w->stream_id, nva, 2, &dr);
      } else {
        h3_respond_status(w->conn, w->stream_id, "501");
      }
    } else {
      if (w->kind == HLS_COLD_LLHLS)
        handled = hls_render_ll(w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, NULL, &resp);
      else if (w->kind == HLS_COLD_DASH)
        handled = dash_render(w->cap_ctx, &w->filter, w->pmt_pid, w->want_ll, reactor_cfg()->dash_utc_url, w->is_head, &resp);
      else
        handled = hls_render(w->cap_ctx, &w->filter, w->pmt_pid, w->container, w->filename, w->is_head, NULL, &resp);
      if (!handled) {
        h3_respond_status(w->conn, w->stream_id, "404");
      } else {
        h3_submit_resp(w->conn, r, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.zc, w->origin[0] ? w->origin : NULL);
        if (resp.status == 200)
          ws_clients_add_bytes(w->ws_handle, resp.body_len);
      }
    }
    fd = w->conn->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
    if (fd >= 0)
      flush_tx(w->conn, fd);
  }
}

#endif /* HAVE_HTTP3 */
