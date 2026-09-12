/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* nghttp3 callbacks + request dispatch: route_parse() -> ts/hls/llhls/dash */

#define _GNU_SOURCE
#ifdef HAVE_HTTP3

#include "../ts/capture/capture.h"
#include "../ts/channels/channels.h"
#include "../hls/hls.h"
#include "../dash/dash.h"
#include "../segment/segment.h"
#include "../ts/lcevcselect.h"
#include "../ts/pidfilter.h"
#include "../ts/pmtselect.h"
#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../reactor/dispatch/route_common.h"
#include "../core/route.h"
#include "../httpng/httpng.h"
#include "http3.h"
#include "http3_int.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <string.h>

static int cb_h3_begin_headers(nghttp3_conn *h3, int64_t sid, void *ud, void *stream_ud) {
  (void)h3;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r) r = alloc_req(c, sid);
  return r ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
}

static int cb_h3_recv_header(nghttp3_conn *h3, int64_t sid, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *ud, void *stream_ud) {
  (void)h3;
  (void)token;
  (void)flags;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r) return 0;
  nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
  nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

  httpng_parse_known_header((const char *)n.base, n.len, (const char *)v.base, v.len,
                            r->method, sizeof r->method, r->path, sizeof r->path,
                            r->inm, sizeof r->inm, r->origin, sizeof r->origin,
                            r->authz, sizeof r->authz, r->protocol, sizeof r->protocol);
  return 0;
}

static int cb_h3_recv_data(nghttp3_conn *h3, int64_t sid, const uint8_t *data, size_t datalen, void *ud, void *stream_ud) {
  (void)h3;
  (void)sid;
  (void)ud;
  if (stream_ud) h3_ws_data_chunk(stream_ud, data, datalen);
  return 0;
}

static int cb_h3_end_headers(nghttp3_conn *h3, int64_t sid, int fin, void *ud, void *stream_ud) {
  (void)h3;
  (void)fin;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r) return 0;
  if (!r->dispatched) r->dispatch_pending = 1;
  return 0;
}

/* ngtcp2 handshake_completed cb: creates H3 session */
int cb_handshake_completed(ngtcp2_conn *qconn, void *ud) {
  h3_conn_t *c = ud;
  c->handshake_done = 1;
  if (ngtcp2_conn_open_uni_stream(qconn, &c->h3_ctrl, NULL) != 0 || ngtcp2_conn_open_uni_stream(qconn, &c->h3_qenc, NULL) != 0 || ngtcp2_conn_open_uni_stream(qconn, &c->h3_qdec, NULL) != 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;

  nghttp3_callbacks h3cbs = {0};
  h3cbs.begin_headers = cb_h3_begin_headers;
  h3cbs.recv_header = cb_h3_recv_header;
  h3cbs.end_headers = cb_h3_end_headers;
  h3cbs.recv_data = cb_h3_recv_data;
  nghttp3_settings h3s;
  nghttp3_settings_default_versioned(NGHTTP3_SETTINGS_VERSION, &h3s);
  h3s.enable_connect_protocol = 1;
  if (nghttp3_conn_server_new_versioned(&c->h3conn, NGHTTP3_CALLBACKS_VERSION, &h3cbs, NGHTTP3_SETTINGS_VERSION, &h3s, NULL, c) != 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;

  nghttp3_conn_bind_control_stream(c->h3conn, c->h3_ctrl);
  nghttp3_conn_bind_qpack_streams(c->h3conn, c->h3_qenc, c->h3_qdec);
  return 0;
}

static int h3_conn_active_count(const h3_conn_t *c) {
  int n = 0;
  for (int i = 0; i < H3_MAX_REQS; i++) if (c->reqs[i].active) n++;
  return n;
}

static void h3_client_ip(const h3_conn_t *c, char *buf, size_t bufsz) {
  const struct sockaddr_storage *sa = &c->peer_addr;
  if (sa->ss_family == AF_INET6)
    inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)sa)->sin6_addr, buf, (socklen_t)bufsz);
  else
    inet_ntop(AF_INET, &((const struct sockaddr_in *)sa)->sin_addr, buf, (socklen_t)bufsz);
}

static void h3ops_respond_status(void *connv, void *reqv, const char *status) {
  h3_respond_status(connv, ((h3_req_t *)reqv)->stream_id, status);
}

static void h3ops_respond_401(void *connv, void *reqv) {
  h3_respond_401(connv, ((h3_req_t *)reqv)->stream_id);
}

static void h3ops_respond_hls(void *connv, void *reqv, int handled, const hls_resp_t *resp, const char *origin_hdr) {
  (void)origin_hdr; /* h3_respond_hls reads r->origin itself, set from headers already */
  h3_respond_hls(connv, reqv, handled, resp);
}

static void h3ops_ws_dispatch(void *connv, void *reqv) { h3_ws_dispatch(connv, reqv); }

static int h3ops_admission_ok(void *connv) {
  return h3_conn_active_count(connv) <= H3_MAX_REQS - H3_WS_RESERVE;
}

static int h3ops_tspush_dispatch(void *connv, void *reqv, int sub) { return h3_tspush_dispatch(connv, reqv, sub); }

static int h3ops_dashchunk_dispatch(void *connv, void *reqv, int sub, int ws_handle) { return h3_dashchunk_dispatch(connv, reqv, sub, ws_handle); }

static int h3ops_mp4push_dispatch(void *connv, void *reqv, int sub, int ws_handle) { return h3_mp4push_dispatch(connv, reqv, sub, ws_handle); }

static int h3ops_hls_cold_try_park(void *connv, void *reqv, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, hls_cold_kind_t kind,
                                   seg_container_t container, int want_ll, int is_head, const char *origin_hdr, int timeout_ms, int ws_handle) {
  return h3_hls_cold_try_park(connv, ((h3_req_t *)reqv)->stream_id, ctx, filter, pmt_pid, lcevc, filename, kind, container, want_ll, is_head, origin_hdr, timeout_ms, ws_handle);
}

static int h3ops_llhls_try_park(void *connv, void *reqv, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head,
                                const char *inm, const char *origin_hdr, uint32_t want_seg, int want_part, int timeout_ms, int ws_handle) {
  return h3_llhls_try_park(connv, ((h3_req_t *)reqv)->stream_id, ctx, filter, pmt_pid, lcevc, filename, is_head, inm, origin_hdr, want_seg, want_part, timeout_ms, ws_handle);
}

static const httpng_ops_t h3_httpng_ops = {
    .proto = 3,
    .respond_status = h3ops_respond_status,
    .respond_401 = h3ops_respond_401,
    .respond_hls = h3ops_respond_hls,
    .ws_dispatch = h3ops_ws_dispatch,
    .admission_ok = h3ops_admission_ok,
    .tspush_dispatch = h3ops_tspush_dispatch,
    .dashchunk_dispatch = h3ops_dashchunk_dispatch,
    .mp4push_dispatch = h3ops_mp4push_dispatch,
    .hls_cold_try_park = h3ops_hls_cold_try_park,
    .llhls_try_park = h3ops_llhls_try_park,
};

void dispatch_req(h3_conn_t *c, h3_req_t *r) {
  httpng_req_hdrs_t hdrs;
  char client_ip[64];
  if (r->dispatched) return;
  r->dispatched = 1;
  h3_client_ip(c, client_ip, sizeof client_ip);
  hdrs.method = r->method;
  hdrs.path = r->path;
  hdrs.inm = r->inm;
  hdrs.origin = r->origin;
  hdrs.authz = r->authz;
  hdrs.protocol = r->protocol;
  httpng_dispatch(&h3_httpng_ops, c, r, &hdrs, client_ip, -1);
}

#endif /* HAVE_HTTP3 */
