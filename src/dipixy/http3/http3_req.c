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
#include "../core/metrics.h"
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
    &(httpng_hdr_out_t){r->method, sizeof r->method, r->path, H3_PATH_MAX,
    r->inm, sizeof r->inm, r->origin, sizeof r->origin, r->authz, sizeof r->authz, r->protocol, sizeof r->protocol, r->host, sizeof r->host,
    r->soapaction, sizeof r->soapaction, r->sid, sizeof r->sid,
    r->callback, sizeof r->callback, r->nt, sizeof r->nt, r->timeout, sizeof r->timeout});
  return 0;
}

static void h3_req_body_append(h3_req_t *r, const uint8_t *data, size_t len) {
  if (r->body_over) return;
  if (r->body_len + len > HTTPNG_BODY_MAX) {
    r->body_over = 1;
    return;
  }
  if (!r->body) {
    r->body = malloc(HTTPNG_BODY_MAX + 1);
    if (!r->body) {
      r->body_over = 1;
      return;
    }
  }
  memcpy(r->body + r->body_len, data, len);
  r->body_len += len;
  r->body[r->body_len] = '\0';
}

static int cb_h3_recv_data(nghttp3_conn *h3, int64_t sid, const uint8_t *data, size_t datalen, void *ud, void *stream_ud) {
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  (void)h3;
  if (r && r->body_wait) h3_req_body_append(r, data, datalen);
  else if (stream_ud) h3_ws_data_chunk(c, stream_ud, data, datalen);
  ngtcp2_conn_extend_max_stream_offset(c->qconn, sid, datalen);
  ngtcp2_conn_extend_max_offset(c->qconn, datalen);
  return 0;
}

static int cb_h3_end_stream(nghttp3_conn *h3, int64_t sid, void *ud, void *stream_ud) {
  h3_req_t *r = find_req(ud, sid);
  (void)h3;
  (void)stream_ud;
  if (r && r->body_wait && !r->dispatched) r->dispatch_pending = 1;
  return 0;
}

static int cb_h3_end_headers(nghttp3_conn *h3, int64_t sid, int fin, void *ud, void *stream_ud) {
  (void)h3;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r) return 0;
  if (!strcmp(r->method, "POST") && !fin) r->body_wait = 1;
  else if (!r->dispatched) r->dispatch_pending = 1;
  return 0;
}

static int cb_h3_deferred_consume(nghttp3_conn *h3, int64_t sid, size_t consumed, void *ud, void *stream_ud) {
  h3_conn_t *c = ud;
  (void)h3;
  (void)stream_ud;
  ngtcp2_conn_extend_max_stream_offset(c->qconn, sid, consumed);
  ngtcp2_conn_extend_max_offset(c->qconn, consumed);
  return 0;
}

static int cb_h3_acked_stream_data(nghttp3_conn *h3, int64_t sid, uint64_t datalen, void *ud, void *stream_ud) {
  h3_req_t *r = stream_ud;
  (void)h3;
  (void)sid;
  (void)ud;
  if (!r || !r->active) return 0;
  if (r->tspush_sub_idx >= 0) h3_tspush_acked(r, (size_t)datalen);
  else if (r->mp4push_sub_idx >= 0) h3_mp4push_acked(r, (size_t)datalen);
  else if (r->dashchunk_sub_idx >= 0) h3_dashchunk_acked(r, (size_t)datalen);
  else if (r->ws_active) h3_ws_acked(r, (size_t)datalen);
  return 0;
}

/* ngtcp2 handshake_completed cb: creates H3 session */
int cb_handshake_completed(ngtcp2_conn *qconn, void *ud) {
  h3_conn_t *c = ud;
  const unsigned char *alpn = NULL;
  unsigned alpn_len = 0;
  SSL_get0_alpn_selected(c->ssl, &alpn, &alpn_len);
  if (alpn_len != 2 || memcmp(alpn, "h3", 2) != 0) return NGTCP2_ERR_CALLBACK_FAILURE;
  c->handshake_done = 1;
  h3_stateless_offer_token(qconn, (struct sockaddr *)&c->peer_addr, c->peer_addrlen);
  if (ngtcp2_conn_open_uni_stream(qconn, &c->h3_ctrl, NULL) != 0 || ngtcp2_conn_open_uni_stream(qconn, &c->h3_qenc, NULL) != 0 || ngtcp2_conn_open_uni_stream(qconn, &c->h3_qdec, NULL) != 0)
    return NGTCP2_ERR_CALLBACK_FAILURE;

  nghttp3_callbacks h3cbs = {0};
  h3cbs.begin_headers = cb_h3_begin_headers;
  h3cbs.recv_header = cb_h3_recv_header;
  h3cbs.end_headers = cb_h3_end_headers;
  h3cbs.recv_data = cb_h3_recv_data;
  h3cbs.end_stream = cb_h3_end_stream;
  h3cbs.deferred_consume = cb_h3_deferred_consume;
  h3cbs.acked_stream_data = cb_h3_acked_stream_data;
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
  for (int i = 0; i < c->max_reqs; i++) {
    if (c->reqs[i].active) n++;
  }
  return n;
}

void h3_client_ip(const h3_conn_t *c, char *buf, size_t bufsz) {
  sockaddr_ip_str(&c->peer_addr, buf, bufsz);
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
  return h3_conn_active_count(connv) <= ((const h3_conn_t *)connv)->max_reqs - H3_WS_RESERVE;
}

static int h3ops_tspush_dispatch(void *connv, void *reqv, int sub, int rawaudio) { return h3_tspush_dispatch(connv, reqv, sub, rawaudio); }

static int h3ops_ts_cold_try_park(void *connv, void *reqv, const ts_cold_park_req_t *req) {
  return h3_ts_cold_try_park(connv, ((h3_req_t *)reqv)->stream_id, req);
}

static int h3ops_open_try_park(void *connv, void *reqv, capture_open_job_t *job) {
  return h3_open_try_park(connv, ((h3_req_t *)reqv)->stream_id, job);
}

static void h3ops_respond_content(void *connv, void *reqv, int status, const char *content_type, const httpng_hdr_t *extra, size_t nextra, uint8_t *body, size_t len, const char *origin_hdr) {
  h3_submit_resp_ex(connv, reqv, status, content_type, NULL, len, body, NULL, 0, origin_hdr, extra, nextra);
}

static int h3ops_dashchunk_dispatch(void *connv, void *reqv, int sub, int ws_handle) { return h3_dashchunk_dispatch(connv, reqv, sub, ws_handle); }

static int h3ops_mp4push_dispatch(void *connv, void *reqv, int sub, int ws_handle) { return h3_mp4push_dispatch(connv, reqv, sub, ws_handle); }

static int h3ops_hls_cold_try_park(void *connv, void *reqv, const hls_cold_park_req_t *req) {
  return h3_hls_cold_try_park(connv, ((h3_req_t *)reqv)->stream_id, req);
}

static int h3ops_llhls_try_park(void *connv, void *reqv, const llhls_park_req_t *req) {
  return h3_llhls_try_park(connv, ((h3_req_t *)reqv)->stream_id, req);
}

static const httpng_ops_t h3_httpng_ops = {
    .proto = CONN_PROTO_H3,
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
    .ts_cold_try_park = h3ops_ts_cold_try_park,
    .open_try_park = h3ops_open_try_park,
    .respond_content = h3ops_respond_content,
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
  hdrs.host = r->host;
  hdrs.soapaction = r->soapaction;
  hdrs.sid = r->sid;
  hdrs.callback = r->callback;
  hdrs.nt = r->nt;
  hdrs.timeout = r->timeout;
  hdrs.body = r->body;
  hdrs.body_len = r->body_len;
  if (r->body_over) {
    h3_respond_status(c, r->stream_id, "400");
    return;
  }
  httpng_dispatch(&h3_httpng_ops, c, r, &hdrs, client_ip, -1);
}

#endif /* HAVE_HTTP3 */
