/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "../hls/hls.h"
#include "../dash/dash.h"
#include "../reactor/internal.h"
#include "http2.h"
#include "http2_int.h"
#include "lib/helper/ioutil.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
  uint8_t *data;
  size_t len;
  size_t off;
  int zc;
} h2_body_src_t;

#define H2_BODY_SRC_POOL_MAX H2_MAX_STREAMS

static _Thread_local h2_body_src_t *t_h2_body_src_pool[H2_BODY_SRC_POOL_MAX];
static _Thread_local int t_h2_body_src_pool_n;

static h2_body_src_t *h2_body_src_alloc(void) {
  if (t_h2_body_src_pool_n > 0)
    return t_h2_body_src_pool[--t_h2_body_src_pool_n];
  return malloc(sizeof(h2_body_src_t));
}

static void h2_body_src_free(h2_body_src_t *src) {
  if (t_h2_body_src_pool_n < H2_BODY_SRC_POOL_MAX)
    t_h2_body_src_pool[t_h2_body_src_pool_n++] = src;
  else
    free(src);
}

/* safe: nghttp2 never touches source->ptr again after EOF */
static ssize_t body_read_cb(nghttp2_session *ng, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *ud) {
  h2_body_src_t *src = source->ptr;
  size_t n = src->len - src->off;
  (void)ng;
  (void)stream_id;
  (void)ud;
  if (n > length)
    n = length;
  if (n)
    memcpy(buf, src->data + src->off, n);
  src->off += n;
  if (src->off >= src->len) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    hls_resp_body_release(src->data, src->zc);
    h2_body_src_free(src);
  }
  return (ssize_t)n;
}

static const char *h2_status_str(int status) {
  switch (status) {
    case 200: return "200";
    case 304: return "304";
    case 404: return "404";
    default: return "500";
  }
}

static size_t u64_to_dec(char *buf, uint64_t v) {
  char tmp[20];
  size_t n = 0;
  if (!v) {
    buf[0] = '0';
    return 1;
  }
  while (v) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (size_t i = 0; i < n; i++)
    buf[i] = tmp[n - 1 - i];
  return n;
}

void h2_submit_resp(h2_conn_t *conn, int32_t stream_id, int status, const char *content_type, const char *etag,
                     size_t content_length, uint8_t *body, int zc, const char *origin_hdr) {
  const char *status_buf;
  char len_buf[24];
  char etag_buf[56];
  size_t len_n;
  nghttp2_nv nva[6];
  size_t nvlen = 0;
  nghttp2_data_provider dp;
  h2_body_src_t *src;
  int cors_vary;
  const char *cors_val = cors_match(reactor_cfg(), origin_hdr, &cors_vary);

  status_buf = h2_status_str(status);
  nva[nvlen++] = (nghttp2_nv){(uint8_t *)":status", (uint8_t *)status_buf, 7, 3, NGHTTP2_NV_FLAG_NONE};
  if (content_type)
    nva[nvlen++] =
        (nghttp2_nv){(uint8_t *)"content-type", (uint8_t *)content_type, 12, strlen(content_type), NGHTTP2_NV_FLAG_NONE};
  len_n = u64_to_dec(len_buf, (uint64_t)content_length);
  nva[nvlen++] = (nghttp2_nv){(uint8_t *)"content-length", (uint8_t *)len_buf, 14, len_n, NGHTTP2_NV_FLAG_NONE};
  if (etag && etag[0]) {
    size_t elen = strlen(etag);
    etag_buf[0] = '"';
    memcpy(etag_buf + 1, etag, elen);
    etag_buf[1 + elen] = '"';
    nva[nvlen++] = (nghttp2_nv){(uint8_t *)"etag", (uint8_t *)etag_buf, 4, elen + 2, NGHTTP2_NV_FLAG_NONE};
  }
  if (cors_val) {
    nva[nvlen++] = (nghttp2_nv){(uint8_t *)"access-control-allow-origin", (uint8_t *)cors_val, 28, strlen(cors_val),NGHTTP2_NV_FLAG_NONE};
    if (cors_vary)
      nva[nvlen++] = (nghttp2_nv){(uint8_t *)"vary", (uint8_t *)"Origin", 4, 6, NGHTTP2_NV_FLAG_NONE};
  }
  if (!body) {
    nghttp2_submit_response(conn->ng, stream_id, nva, nvlen, NULL);
    return;
  }
  src = h2_body_src_alloc();
  if (!src) {
    hls_resp_body_release(body, zc);
    nghttp2_nv e = MAKE_NV_LIT(":status", "500");
    nghttp2_submit_response(conn->ng, stream_id, &e, 1, NULL);
    return;
  }
  src->data = body;
  src->len = content_length;
  src->off = 0;
  src->zc = zc;
  dp.read_callback = body_read_cb;
  dp.source.ptr = src;
  nghttp2_submit_response(conn->ng, stream_id, nva, nvlen, &dp);
}

#define H2_LLHLS_WAITERS_MAX 8

static _Thread_local llhls_waiter_t t_h2_llhls_waiters[H2_LLHLS_WAITERS_MAX];
static _Thread_local int t_h2_llhls_waiters_active;

int h2_llhls_try_park(h2_conn_t *conn, const conn_t *c, int32_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter,
                       unsigned pmt_pid, const char *filename, int is_head, const char *inm, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle) {
  (void)c; /* recovered from conn->c in h2_llhls_finish() */
  return llhls_waiter_pool_try_park(t_h2_llhls_waiters, H2_LLHLS_WAITERS_MAX, &t_h2_llhls_waiters_active, conn,
                                     stream_id, ctx, filter, pmt_pid, filename, is_head, 0, inm, origin_hdr, want_seg,
                                     want_part, timeout_ms, ws_handle);
}

void h2_llhls_on_stream_close(h2_conn_t *conn, int32_t stream_id) {
  llhls_waiter_pool_close_owner(t_h2_llhls_waiters, H2_LLHLS_WAITERS_MAX, &t_h2_llhls_waiters_active, conn, stream_id);
}

void h2_llhls_on_conn_close(h2_conn_t *conn) {
  llhls_waiter_pool_close_owner(t_h2_llhls_waiters, H2_LLHLS_WAITERS_MAX, &t_h2_llhls_waiters_active, conn, -1);
}

static void h2_llhls_finish(llhls_waiter_t *w) {
  h2_conn_t *conn = w->owner;
  hls_resp_t resp;
  if (!hls_render_ll(w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, w->inm[0] ? w->inm : NULL, &resp)) {
    h2_submit_resp(conn, (int32_t)w->stream_id, 404, NULL, NULL, 0, NULL, 0, w->origin[0] ? w->origin : NULL);
  } else {
    h2_submit_resp(conn, (int32_t)w->stream_id, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body,
                   resp.zc, w->origin[0] ? w->origin : NULL);
    if (resp.status == 200)
      ws_clients_add_bytes(w->ws_handle, resp.body_len);
  }
  h2_flush_tx(conn, conn->c);
}

void h2_llhls_flush_waiters(void) {
  llhls_waiter_pool_flush(t_h2_llhls_waiters, H2_LLHLS_WAITERS_MAX, &t_h2_llhls_waiters_active, h2_llhls_finish);
}

#define H2_HLS_COLD_WAITERS_MAX 64

typedef struct {
  h2_conn_t *conn;
  int32_t stream_id;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  char filename[32];
  hls_cold_kind_t kind;
  seg_container_t container; /* HLS_COLD_HLS only, hls vs hls-fmp4 */
  int want_ll;                /* HLS_COLD_DASH only, lldash vs dash */
  int is_head;
  char origin[128];
  int64_t deadline_ms;
  int active;
  int ws_handle;
} h2_hls_cold_waiter_t;

static _Thread_local h2_hls_cold_waiter_t t_h2_hls_cold_waiters[H2_HLS_COLD_WAITERS_MAX];
static _Thread_local int t_h2_hls_cold_waiters_active;

/* 1 parked: manifest/playlist requested before capture's first segment/part.
   0 table full: caller serves now */
int h2_hls_cold_try_park(h2_conn_t *conn, int32_t stream_id, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, hls_cold_kind_t kind, seg_container_t container,
                         int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle) {
  for (int i = 0; i < H2_HLS_COLD_WAITERS_MAX; i++) {
    h2_hls_cold_waiter_t *w = &t_h2_hls_cold_waiters[i];
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
    t_h2_hls_cold_waiters_active++;
    return 1;
  }
  return 0;
}

void h2_hls_cold_on_stream_close(const h2_conn_t *conn, int32_t stream_id) {
  for (int i = 0; i < H2_HLS_COLD_WAITERS_MAX; i++)
    if (t_h2_hls_cold_waiters[i].active && t_h2_hls_cold_waiters[i].conn == conn && t_h2_hls_cold_waiters[i].stream_id == stream_id) {
      t_h2_hls_cold_waiters[i].active = 0;
      t_h2_hls_cold_waiters_active--;
    }
}

void h2_hls_cold_on_conn_close(const h2_conn_t *conn) {
  for (int i = 0; i < H2_HLS_COLD_WAITERS_MAX; i++)
    if (t_h2_hls_cold_waiters[i].active && t_h2_hls_cold_waiters[i].conn == conn) {
      t_h2_hls_cold_waiters[i].active = 0;
      t_h2_hls_cold_waiters_active--;
    }
}

void h2_hls_cold_flush_waiters(void) {
  int64_t now;
  if (!t_h2_hls_cold_waiters_active)
    return;
  now = now_ms();
  for (int i = 0; i < H2_HLS_COLD_WAITERS_MAX; i++) {
    h2_hls_cold_waiter_t *w = &t_h2_hls_cold_waiters[i];
    int ready, handled;
    hls_resp_t resp;
    if (!w->active) continue;
    ready = w->kind == HLS_COLD_LLHLS ? hls_ll_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container) : hls_store_ready(w->cap_ctx, &w->filter, w->pmt_pid, w->container);
    if (now < w->deadline_ms && !ready) continue;
    if (w->kind == HLS_COLD_LLHLS)
      handled = hls_render_ll(w->cap_ctx, &w->filter, w->pmt_pid, w->filename, w->is_head, NULL, &resp);
    else if (w->kind == HLS_COLD_DASH)
      handled = dash_render(w->cap_ctx, &w->filter, w->pmt_pid, w->want_ll, reactor_cfg()->dash_utc_url, w->is_head, &resp);
    else
      handled = hls_render(w->cap_ctx, &w->filter, w->pmt_pid, w->container, w->filename, w->is_head, NULL, &resp);
    if (!handled) {
      h2_submit_resp(w->conn, w->stream_id, 404, NULL, NULL, 0, NULL, 0, w->origin[0] ? w->origin : NULL);
    } else {
      h2_submit_resp(w->conn, w->stream_id, resp.status, resp.content_type, resp.etag, resp.body_len, resp.body, resp.zc, w->origin[0] ? w->origin : NULL);
      if (resp.status == 200)
        ws_clients_add_bytes(w->ws_handle, resp.body_len);
    }
    h2_flush_tx(w->conn, w->conn->c);
    w->active = 0;
    t_h2_hls_cold_waiters_active--;
  }
}

#endif /* HAVE_HTTP2 */
