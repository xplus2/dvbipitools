/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "http2.h"
#include "http2_int.h"

#include "../ts/capture/capture.h"
#include "../ts/channels/channels.h"
#include "../hls/hls.h"
#include "../dash/dash.h"
#include "../dash/lldash.h"
#include "../segment/segment.h"
#include "../segment/mp4push.h"
#include "../ts/lcevcselect.h"
#include "../ts/pidfilter.h"
#include "../ts/pmtselect.h"
#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../reactor/dispatch/route_common.h"
#include "../core/route.h"
#include "../ts/ts_push.h"
#include "../httpng/httpng.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define H2_READ_BUF 65536
#define H2_READ_MAX_ITER 16

static h2_stream_t *h2_find_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++) if (conn->streams[i].id == id) return &conn->streams[i];
  return NULL;
}

static h2_stream_t *h2_alloc_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++) if (!conn->streams[i].id) {
    memset(&conn->streams[i], 0, sizeof(h2_stream_t));
    conn->streams[i].id = id;
    return &conn->streams[i];
  }
  return NULL;
}

static void h2_free_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++) if (conn->streams[i].id == id) conn->streams[i].id = 0;
}

static int h2_conn_active_count(const h2_conn_t *conn) {
  int n = 0;
  for (int i = 0; i < H2_MAX_STREAMS; i++) if (conn->streams[i].id) n++;
  return n;
}

static int cb_begin_headers(nghttp2_session *ng, const nghttp2_frame *frame, void *ud) {
  h2_conn_t *conn = ud;
  if (frame->hd.type != NGHTTP2_HEADERS) return 0;
  if (frame->headers.cat == NGHTTP2_HCAT_REQUEST) {
    conn->hdr_stream = h2_alloc_stream(conn, frame->hd.stream_id);
    if (!conn->hdr_stream) nghttp2_submit_rst_stream(ng, NGHTTP2_FLAG_NONE, frame->hd.stream_id, NGHTTP2_REFUSED_STREAM);
  } else {
    conn->hdr_stream = h2_find_stream(conn, frame->hd.stream_id);
  }
  return 0;
}

static int cb_on_header(nghttp2_session *ng, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *ud) {
  (void)ng;
  (void)flags;
  (void)frame;
  h2_conn_t *conn = ud;
  h2_stream_t *s = conn->hdr_stream;
  if (!s) return 0;
  httpng_parse_known_header((const char *)name, namelen, (const char *)value, valuelen,
                            s->method, sizeof s->method, s->path, sizeof s->path,
                            s->inm, sizeof s->inm, s->origin, sizeof s->origin,
                            s->authz, sizeof s->authz, s->protocol, sizeof s->protocol);
  return 0;
}

static int cb_data_chunk_recv(nghttp2_session *ng, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t len, void *ud) {
  (void)ng;
  (void)flags;
  h2_ws_data_chunk(ud, stream_id, data, len);
  return 0;
}

static int cb_frame_recv(nghttp2_session *ng, const nghttp2_frame *frame, void *ud) {
  (void)ng;
  h2_conn_t *conn = ud;
  if (frame->hd.type == NGHTTP2_HEADERS && (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS)) {
    h2_stream_t *s = conn->hdr_stream;
    if (s && s->id == frame->hd.stream_id && !s->dispatch_pending) {
      s->dispatch_pending = 1;
      conn->pending[conn->pending_n++] = s;
    }
  }
  if (frame->hd.type == NGHTTP2_GOAWAY) conn->done = 1;
  return 0;
}

static int cb_stream_close(nghttp2_session *ng, int32_t stream_id, uint32_t error_code, void *ud) {
  (void)ng;
  (void)error_code;
  h2_conn_t *conn = ud;
  h2_free_stream(conn, stream_id);
  h2_tspush_on_stream_close(conn, stream_id);
  h2_dashchunk_on_stream_close(conn, stream_id);
  h2_mp4push_on_stream_close(conn, stream_id);
  h2_llhls_on_stream_close(conn, stream_id);
  h2_hls_cold_on_stream_close(conn, stream_id);
  h2_ws_on_stream_close(conn, stream_id);
  return 0;
}

void h2_wake_stream(conn_t *c, int32_t sid) {
  if (!c || !sid) return;
  nghttp2_session_resume_data(((h2_conn_t *)c->h2)->ng, sid);
  if (!atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed)) conn_epoll_mod(c, c->epfd, 1);
}

void h2_flush_tx(h2_conn_t *conn, conn_t *c) {
  const uint8_t *out;
  ssize_t outlen;
  while ((outlen = nghttp2_session_mem_send(conn->ng, &out)) > 0)
    if (conn_queue(c, (const char *)out, (size_t)outlen) < 0) {
      conn->done = 1;
      break;
    }
  conn_flush(c, c->epfd);
}

static void h2_respond_status(h2_conn_t *conn, int32_t stream_id, const char *status) {
  nghttp2_nv nva[] = {{(uint8_t *)":status", (uint8_t *)status, 7, strlen(status), NGHTTP2_NV_FLAG_NONE}};
  nghttp2_submit_response(conn->ng, stream_id, nva, 1, NULL);
}

static void h2_respond_401(h2_conn_t *conn, int32_t stream_id) {
  nghttp2_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"401", 7, 3, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)"www-authenticate", (uint8_t *)"Basic realm=\"dipixy\"", 16, 20, NGHTTP2_NV_FLAG_NONE},
  };
  nghttp2_submit_response(conn->ng, stream_id, nva, 2, NULL);
}

static void h2_respond_hls(h2_conn_t *conn, int32_t stream_id, int handled, const hls_resp_t *resp, const char *origin_hdr) {
  if (!handled)
    h2_respond_status(conn, stream_id, "404");
  else
    h2_submit_resp(conn, stream_id, resp->status, resp->content_type, resp->etag, resp->body_len, resp->body, resp->zc, origin_hdr);
}

/* httpng_dispatch() opaque req: h2 per-request state spans transient stream
   slot plus owning conn_t (out_lock/epfd), unlike h3's single h3_req_t */
typedef struct {
  conn_t *c;
  h2_stream_t *stream;
} h2_req_ctx_t;

static void h2ops_respond_status(void *connv, void *reqv, const char *status) {
  h2_respond_status(connv, ((h2_req_ctx_t *)reqv)->stream->id, status);
}

static void h2ops_respond_401(void *connv, void *reqv) {
  h2_respond_401(connv, ((h2_req_ctx_t *)reqv)->stream->id);
}

static void h2ops_respond_hls(void *connv, void *reqv, int handled, const hls_resp_t *resp, const char *origin_hdr) {
  h2_respond_hls(connv, ((h2_req_ctx_t *)reqv)->stream->id, handled, resp, origin_hdr);
}

static void h2ops_ws_dispatch(void *connv, void *reqv) {
  h2_req_ctx_t *rq = reqv;
  h2_ws_dispatch(connv, rq->c, rq->stream->id);
}

static int h2ops_admission_ok(void *connv) {
  return h2_conn_active_count(connv) <= H2_MAX_STREAMS - H2_WS_RESERVE;
}

static int h2ops_tspush_dispatch(void *connv, void *reqv, int sub) {
  h2_req_ctx_t *rq = reqv;
  return h2_tspush_dispatch(connv, rq->c, rq->stream->id, sub);
}

static int h2ops_dashchunk_dispatch(void *connv, void *reqv, int sub, int ws_handle) {
  h2_req_ctx_t *rq = reqv;
  return h2_dashchunk_dispatch(connv, rq->c, rq->stream->id, sub, ws_handle);
}

static int h2ops_mp4push_dispatch(void *connv, void *reqv, int sub, int ws_handle) {
  h2_req_ctx_t *rq = reqv;
  return h2_mp4push_dispatch(connv, rq->c, rq->stream->id, sub, ws_handle);
}

static int h2ops_hls_cold_try_park(void *connv, void *reqv, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, hls_cold_kind_t kind,
                                   seg_container_t container, int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle) {
  const h2_req_ctx_t *rq = reqv;
  return h2_hls_cold_try_park(connv, rq->stream->id, ctx, filter, pmt_pid, lcevc, filename, kind, container, want_ll, is_head, origin_hdr, timeout_ms, ws_handle);
}

static int h2ops_llhls_try_park(void *connv, void *reqv, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head,
                                const char *inm, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle) {
  const h2_req_ctx_t *rq = reqv;
  return h2_llhls_try_park(connv, rq->c, rq->stream->id, ctx, filter, pmt_pid, lcevc, filename, is_head, inm, origin_hdr, want_seg, want_part, timeout_ms, ws_handle);
}

static const httpng_ops_t h2_httpng_ops = {
    .proto = 2,
    .respond_status = h2ops_respond_status,
    .respond_401 = h2ops_respond_401,
    .respond_hls = h2ops_respond_hls,
    .ws_dispatch = h2ops_ws_dispatch,
    .admission_ok = h2ops_admission_ok,
    .tspush_dispatch = h2ops_tspush_dispatch,
    .dashchunk_dispatch = h2ops_dashchunk_dispatch,
    .mp4push_dispatch = h2ops_mp4push_dispatch,
    .hls_cold_try_park = h2ops_hls_cold_try_park,
    .llhls_try_park = h2ops_llhls_try_park,
};

static void h2_dispatch_stream(h2_conn_t *conn, conn_t *c, h2_stream_t *stream) {
  h2_req_ctx_t rq = {c, stream};
  httpng_req_hdrs_t hdrs = {stream->method, stream->path, stream->inm, stream->origin, stream->authz, stream->protocol};
  httpng_dispatch(&h2_httpng_ops, conn, &rq, &hdrs, c->client_ip, c->fd);
}

int h2_conn_attach(conn_t *c) {
  nghttp2_session_callbacks *cbs;
  if (nghttp2_session_callbacks_new(&cbs) != 0) return -1;
  nghttp2_session_callbacks_set_on_begin_headers_callback(cbs, cb_begin_headers);
  nghttp2_session_callbacks_set_on_header_callback(cbs, cb_on_header);
  nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, cb_frame_recv);
  nghttp2_session_callbacks_set_on_stream_close_callback(cbs, cb_stream_close);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, cb_data_chunk_recv);

  h2_conn_t *conn = calloc(1, sizeof(h2_conn_t));
  if (!conn) {
    nghttp2_session_callbacks_del(cbs);
    return -1;
  }
  conn->fd = c->fd;
  conn->c = c;
  if (nghttp2_session_server_new(&conn->ng, cbs, conn) != 0) {
    nghttp2_session_callbacks_del(cbs);
    free(conn);
    return -1;
  }
  nghttp2_session_callbacks_del(cbs);
  nghttp2_settings_entry iv[] = {
      {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, H2_MAX_STREAMS},
      {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 65535},
      {NGHTTP2_SETTINGS_ENABLE_CONNECT_PROTOCOL, 1},
  };
  nghttp2_submit_settings(conn->ng, NGHTTP2_FLAG_NONE, iv, sizeof iv / sizeof iv[0]);
  c->h2 = conn;
  c->state = CONN_H2;
  conn_publish(c);
  h2_flush_tx(conn, c); /* flush initial SETTINGS, might arm EPOLLOUT */
  return 0;
}

void h2_handle_readable(int epfd, conn_t *c) {
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  uint8_t inbuf[H2_READ_BUF];
  conn->pending_n = 0;
  for (int iter = 0; iter < H2_READ_MAX_ITER; iter++) {
    ssize_t nread = tls_net_recv(c->fd, inbuf, sizeof inbuf);
    if (nread > 0) {
      ssize_t consumed = nghttp2_session_mem_recv(conn->ng, inbuf, (size_t)nread);
      if (consumed < 0) {
        h2_conn_close(epfd, c);
        return;
      }
      continue;
    }
    if (nread == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      h2_conn_close(epfd, c);
      return;
    }
    break; /* EAGAIN */
  }

  for (int i = 0; i < conn->pending_n; i++) {
    h2_stream_t *s = conn->pending[i];
    if (!s->id || !s->dispatch_pending) continue;
    int32_t sid = s->id;
    s->dispatch_pending = 0;
    h2_dispatch_stream(conn, c, s);
    h2_free_stream(conn, sid);
  }
  if (conn->done || (!nghttp2_session_want_read(conn->ng) && !nghttp2_session_want_write(conn->ng))) {
    h2_conn_close(epfd, c);
    return;
  }

  h2_ws_flush(conn);
  h2_flush_tx(conn, c);
}

void h2_handle_writable(int epfd, conn_t *c) {
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  int r = conn_flush(c, c->epfd);
  if (r == CONN_FLUSH_ERROR) {
    h2_conn_close(epfd, c);
    return;
  }
  if (r == CONN_FLUSH_MORE) return; /* still backpressured, retry next EPOLLOUT */
  h2_ws_flush(conn);
  const uint8_t *out;
  ssize_t outlen;
  while ((outlen = nghttp2_session_mem_send(conn->ng, &out)) > 0)
    if (conn_queue(c, (const char *)out, (size_t)outlen) < 0) {
      conn->done = 1;
      break;
    }
  conn_flush(c, c->epfd);
  if (conn->done || (!nghttp2_session_want_read(conn->ng) && !nghttp2_session_want_write(conn->ng)))
    h2_conn_close(epfd, c);
}

void h2_conn_close(int epfd, conn_t *c) {
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  for (int i = 0; i < H2_TSPUSH_MAX; i++) if (conn->tspush[i].sid) h2_tspush_on_stream_close(conn, conn->tspush[i].sid);
  for (int i = 0; i < H2_DASHCHUNK_MAX; i++) if (conn->dashchunk[i].sid) h2_dashchunk_on_stream_close(conn, conn->dashchunk[i].sid);
  for (int i = 0; i < H2_MP4PUSH_MAX; i++) if (conn->mp4push[i].sid) h2_mp4push_on_stream_close(conn, conn->mp4push[i].sid);
  h2_llhls_on_conn_close(conn);
  h2_hls_cold_on_conn_close(conn);
  h2_ws_on_conn_close(conn);
  nghttp2_session_del(conn->ng);
  free(conn);
  c->h2 = NULL;
  conn_unpublish(c);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  tls_close_fd(c->fd);
  conn_free(c);
}

#endif /* HAVE_HTTP2 */
