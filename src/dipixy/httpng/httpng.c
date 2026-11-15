/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#if defined(HAVE_HTTP2) || defined(HAVE_HTTP3)

#include "httpng.h"

#include "../core/metrics.h"
#include "../core/route.h"
#include "../core/time_ep.h"
#include "../dash/dash.h"
#include "../dash/lldash.h"
#include "../hls/hls.h"
#include "../reactor/dispatch/route_common.h"
#include "../segment/mp4push.h"
#include "../ts/lcevcselect.h"
#include "../ts/pmtselect.h"
#include "../ts/ts_push.h"
#include "../ws/ws_clients.h"

#include <stdlib.h>
#include <string.h>

size_t httpng_u64_to_dec(char *buf, uint64_t v) {
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
  for (size_t i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
  return n;
}

const char *httpng_status_str(int status) {
  switch (status) {
    case 200: return "200";
    case 304: return "304";
    case 400: return "400";
    case 403: return "403";
    case 404: return "404";
    case 405: return "405";
    case 412: return "412";
    case 501: return "501";
    case 503: return "503";
    case 504: return "504";
    default: return "500";
  }
}

size_t httpng_format_etag(char *etag_buf, size_t etag_buf_sz, const char *etag) {
  size_t elen;
  if (!etag || !etag[0]) return 0;
  elen = strlen(etag);
  if (elen > etag_buf_sz - 2) elen = etag_buf_sz - 2; /* independent of hls_resp_t.etag size */
  etag_buf[0] = '"';
  memcpy(etag_buf + 1, etag, elen);
  etag_buf[1 + elen] = '"';
  return elen + 2;
}

#define HDR_IS(lit) (!memcmp(name, lit, sizeof lit - 1))

void httpng_parse_known_header(const char *name, size_t namelen, const char *value, size_t valuelen, const httpng_hdr_out_t *out) {
  char *dst = NULL;
  size_t dsz = 0;
  switch (namelen) {
    case 2:
      if (HDR_IS("nt")) dst = out->nt, dsz = out->nt_sz;
      break;
    case 3:
      if (HDR_IS("sid")) dst = out->sid, dsz = out->sid_sz;
      break;
    case 4:
      if (HDR_IS("host")) dst = out->host, dsz = out->host_sz;
      break;
    case 5:
      if (HDR_IS(":path")) dst = out->path, dsz = out->path_sz;
      break;
    case 6:
      if (HDR_IS("origin")) dst = out->origin, dsz = out->origin_sz;
      break;
    case 7:
      if (HDR_IS(":method")) dst = out->method, dsz = out->method_sz;
      else if (HDR_IS("timeout")) dst = out->timeout, dsz = out->timeout_sz;
      break;
    case 8:
      if (HDR_IS("callback")) dst = out->callback, dsz = out->callback_sz;
      break;
    case 9:
      if (HDR_IS(":protocol")) dst = out->protocol, dsz = out->protocol_sz;
      break;
    case 10:
      if (HDR_IS(":authority")) dst = out->host, dsz = out->host_sz;
      else if (HDR_IS("soapaction")) dst = out->soapaction, dsz = out->soapaction_sz;
      break;
    case 13:
      if (HDR_IS("if-none-match")) dst = out->inm, dsz = out->inm_sz;
      else if (HDR_IS("authorization")) dst = out->authz, dsz = out->authz_sz;
      break;
    default:
      break;
  }
  if (dst) hdr_value_copy(dst, dsz, (const uint8_t *)value, valuelen);
}

#undef HDR_IS

int httpng_ws_drain_pending(pthread_mutex_t *lock, uint8_t **pending, size_t *pending_len, size_t *pending_cap, uint8_t **out_data, size_t *out_len) {
  uint8_t *data;
  size_t len;
  pthread_mutex_lock(lock);
  data = *pending;
  len = *pending_len;
  *pending = NULL;
  *pending_len = 0;
  *pending_cap = 0;
  pthread_mutex_unlock(lock);
  if (!len) {
    free(data);
    return 0;
  }
  *out_data = data;
  *out_len = len;
  return 1;
}

int httpng_hls_cold_render(hls_cold_kind_t kind, const stream_key_t *k, int want_ll, const char *utc_url, const route_file_t *f, int is_head, hls_resp_t *out) {
  const stream_key_t *fk = STREAM_KEY_AS(k, SEG_CONTAINER_FMP4);
  switch (kind) {
    case HLS_COLD_LLHLS:
      return hls_render_ll(k, hls_store_find(k), f, is_head, NULL, out);
    case HLS_COLD_DASH:
      return dash_render(fk, hls_store_find(fk), want_ll, utc_url, is_head, out);
    case HLS_COLD_HLS:
    case HLS_COLD_MP4:
    default:
      return hls_render(k, hls_store_find(k), f, is_head, NULL, out);
  }
}

int httpng_ts_cold_subscribe(ts_cold_waiter_t *w, conn_proto_t proto, int fd, const char *client_ip, int *sub, const char **err) {
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  if (!capture_ctx_bytes(w->cap_ctx)) {
    capture_close(w->cap_ctx);
    *err = "504";
    return 0;
  }
  route_client_info(&w->rt, w->list_num, &w->filter, w->tp_pmt_pid, client_ip, proto, &item_bufs, &cinfo);
  *sub = ts_push_subscribe(w->cap_ctx, &w->filter, proto, fd, w->tp_pmt_pid, w->spts, w->rawaudio, &cinfo, &w->lcevc);
  if (*sub < 0) {
    capture_close(w->cap_ctx);
    *err = "503";
    return 0;
  }
  return 1;
}

static void park_open(httpng_req_t *rq, capture_open_job_t *job) {
  if (rq->ops->open_try_park(rq->conn, rq->req, job)) {
    rq->opening = 1;
    return;
  }
  capture_open_job_cancel(job);
  rq->ops->respond_status(rq->conn, rq->req, 503);
}

void dispatch_ts_route(httpng_req_t *rq) {
  unsigned list_num;
  capture_ctx_t *ctx;
  capture_open_job_t *job;
  open_res_t res;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  int sub;
  int spts = rq->rt->fmt == ROUTE_FMT_SPTS;
  int rawaudio = rq->rt->fmt == ROUTE_FMT_RAWAUDIO;
  unsigned tp_pmt_pid = rq->rt->fmt == ROUTE_FMT_TS ? 0 : rq->pmt_pid;
  if (!rq->is_head && !rq->ops->admission_ok(rq->conn)) {
    rq->ops->respond_status(rq->conn, rq->req, 503);
    return;
  }
  res = open_source_try(rq->rt, &list_num, &ctx, &job);
  if (res == OPEN_RES_PENDING) {
    park_open(rq, job);
    return;
  }
  if (res == OPEN_RES_FAIL) {
    rq->ops->respond_status(rq->conn, rq->req, 404);
    return;
  }
  if (rq->is_head) {
    capture_close(ctx);
    rq->ops->respond_content(rq->conn, rq->req, 200, rawaudio ? "audio/mpeg" : "video/mp2t", NULL, 0, NULL, HTTPNG_NO_LENGTH, NULL);
    return;
  }
  if (!capture_ctx_bytes(ctx) && reactor_cfg()->ts_startup_timeout_s > 0.0) {
    ts_cold_park_req_t pr = {ctx, rq->rt, list_num, rq->filter, tp_pmt_pid, rq->lcevc, spts, rawaudio, 0, (int)(reactor_cfg()->ts_startup_timeout_s * 1000.0)};
    if (rq->ops->ts_cold_try_park(rq->conn, rq->req, &pr)) return;
  }
  route_client_info(rq->rt, list_num, rq->filter, tp_pmt_pid, rq->client_ip, rq->ops->proto, &item_bufs, &cinfo);
  sub = ts_push_subscribe(ctx, rq->filter, rq->ops->proto, rq->fd, tp_pmt_pid, spts, rawaudio, &cinfo, rq->lcevc);
  if (sub < 0) {
    capture_close(ctx);
    rq->ops->respond_status(rq->conn, rq->req, 503);
    return;
  }
  if (!rq->ops->tspush_dispatch(rq->conn, rq->req, sub, rawaudio)) {
    ts_push_unsubscribe_by_idx(sub);
    rq->ops->respond_status(rq->conn, rq->req, 503);
  }
}

static void ng_status(void *t, int code) {
  const httpng_req_t *rq = t;
  rq->ops->respond_status(rq->conn, rq->req, code);
}

static int ng_park_open(void *t, capture_open_job_t *job) {
  httpng_req_t *rq = t;
  if (!rq->ops->open_try_park(rq->conn, rq->req, job)) return 0;
  rq->opening = 1;
  return 1;
}

static int ng_cold_park(void *t, const hls_cold_park_req_t *pr) {
  const httpng_req_t *rq = t;
  return rq->ops->hls_cold_try_park(rq->conn, rq->req, pr);
}

static int ng_ll_park(void *t, const llhls_park_req_t *pr) {
  const httpng_req_t *rq = t;
  return rq->ops->llhls_try_park(rq->conn, rq->req, pr);
}

static size_t ng_respond(const httpng_req_t *rq, int handled, const hls_resp_t *resp) {
  rq->ops->respond_hls(rq->conn, rq->req, handled, resp, rq->origin);
  return handled && resp->status == 200 ? resp->body_len : 0;
}

static size_t ng_hls(void *t, const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int ll) {
  const httpng_req_t *rq = t;
  hls_resp_t resp;
  int handled = ll ? hls_render_ll(k, st, f, rq->is_head, rq->inm, &resp) : hls_render(k, st, f, rq->is_head, rq->inm, &resp);
  return ng_respond(rq, handled, &resp);
}

static size_t ng_dash(void *t, const stream_key_t *k, const hls_store_t *st, int want_ll, const char *utc_url) {
  const httpng_req_t *rq = t;
  hls_resp_t resp;
  return ng_respond(rq, dash_render(k, st, want_ll, utc_url, rq->is_head, &resp), &resp);
}

static size_t ng_dash_seg(void *t, const stream_key_t *k, const route_file_t *f) {
  const httpng_req_t *rq = t;
  hls_resp_t resp;
  return ng_respond(rq, dash_render_seg(k, f, rq->is_head, &resp), &resp);
}

static int ng_lldash(void *t, const stream_key_t *k, const route_file_t *f, int ws_handle) {
  const httpng_req_t *rq = t;
  int sub = dash_lldash_subscribe(k, f, rq->ops->proto);
  if (sub < 0) return 0;
  if (rq->ops->dashchunk_dispatch(rq->conn, rq->req, sub, ws_handle)) return 1;
  dash_lldash_sub_close(sub);
  return 0;
}

static void ng_mp4(void *t, const stream_key_t *k, int ws_handle) {
  const httpng_req_t *rq = t;
  int sub = mp4push_subscribe(k, rq->ops->proto);
  if (sub >= 0 && rq->ops->mp4push_dispatch(rq->conn, rq->req, sub, ws_handle)) return;
  if (sub >= 0) mp4push_sub_close(sub);
  rq->ops->respond_status(rq->conn, rq->req, 503);
}

void dispatch_store_route(httpng_req_t *rq) {
  const route_io_ops_t ops = {ng_status, ng_park_open, ng_cold_park, ng_ll_park, ng_hls, ng_dash, ng_dash_seg, ng_lldash, ng_mp4};
  route_dispatch_store(&(route_io_t){&ops, rq, rq->rt, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->is_head, 0, rq->origin, rq->inm, rq->query, rq->host, 1,
                                     rq->item_bufs, rq->cinfo});
}

/* matches reactor/dispatch.c's HTTP/1.1 dispatch */
void httpng_dispatch(const httpng_ops_t *ops, void *conn, void *req, const httpng_req_hdrs_t *hdrs, const char *client_ip, int fd) {
  route_t rt;
  pid_filter_t filter;
  lcevc_select_t lcevc;
  route_item_bufs_t item_bufs;
  client_info_t cinfo;
  char *qmark;
  char *query;
  const char *want_auth;
  httpng_req_t rq;
  http_method_t method = parse_http_method(hdrs->method, strlen(hdrs->method));
  special_path_t sp;

  if (!t_open_replay) dipixy_metrics_note_request();
  if (method == HTTP_METHOD_CONNECT && !strcmp(hdrs->protocol, "websocket")) {
    if (reactor_cfg()->no_status || special_path_kind(hdrs->path) != SPECIAL_UI_WS)
      ops->respond_status(conn, req, 404);
    else if (!ws_origin_ok(reactor_cfg(), hdrs->origin, hdrs->host))
      ops->respond_status(conn, req, 403);
    else if (!http_auth_ok(reactor_cfg()->http_auth, hdrs->authz[0] ? hdrs->authz : NULL))
      ops->respond_401(conn, req);
    else
      ops->ws_dispatch(conn, req);
    return;
  }

  qmark = strchr(hdrs->path, '?');
  query = qmark ? qmark + 1 : NULL;
  if (qmark) *qmark = '\0';
  sp = special_path_kind(hdrs->path);

  switch (method) {
    case HTTP_METHOD_GET:
    case HTTP_METHOD_HEAD:
      break;
    case HTTP_METHOD_POST:
    case HTTP_METHOD_SUBSCRIBE:
    case HTTP_METHOD_UNSUBSCRIBE:
      httpng_dispatch_dlna(ops, conn, req, method, sp, hdrs, client_ip);
      return;
    case HTTP_METHOD_CONNECT:
    case HTTP_METHOD_OTHER:
      ops->respond_status(conn, req, 405);
      return;
  }
  want_auth = special_path_auth(sp);
  if (want_auth && !http_auth_ok(want_auth, hdrs->authz[0] ? hdrs->authz : NULL)) {
    ops->respond_401(conn, req);
    return;
  }
  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters) pid_filter_parse_query(query, &filter);
  lcevc_select_parse_query(reactor_cfg()->no_lcevc ? NULL : query, &lcevc);
  rq.ops = ops;
  rq.conn = conn;
  rq.req = req;
  rq.rt = &rt;
  rq.filter = &filter;
  rq.pmt_pid = pmt_select_parse_query(query);
  rq.lcevc = &lcevc;
  rq.item_bufs = &item_bufs;
  rq.cinfo = &cinfo;
  rq.client_ip = client_ip;
  rq.fd = fd;
  rq.is_head = method == HTTP_METHOD_HEAD;
  rq.origin = hdrs->origin[0] ? hdrs->origin : NULL;
  rq.inm = hdrs->inm[0] ? hdrs->inm : NULL;
  rq.query = query;
  rq.host = hdrs->host;
  rq.opening = 0;
  if (httpng_dispatch_special(ops, conn, req, sp, hdrs, query, &filter, &lcevc, rq.is_head)) return;
  if (route_parse(hdrs->path, &rt) || route_disabled(&rt)) {
    ops->respond_status(conn, req, 404);
    return;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS:
    case ROUTE_FMT_RAWAUDIO:
      dispatch_ts_route(&rq);
      break;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4:
    case ROUTE_FMT_LLHLS:
    case ROUTE_FMT_LLHLS_FMP4:
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH:
    case ROUTE_FMT_MP4:
      dispatch_store_route(&rq);
      break;
    default:
      ops->respond_status(conn, req, 501);
      break;
  }
  if (rq.opening && qmark) *qmark = '?';
}

#define HTTPNG_BODY_POOL_MAX 8

static _Thread_local uint8_t *t_body_pool[HTTPNG_BODY_POOL_MAX];
static _Thread_local int t_body_pool_n;

uint8_t *httpng_body_get(void) { return t_body_pool_n ? t_body_pool[--t_body_pool_n] : malloc(HTTPNG_BODY_MAX + 1); }

void httpng_body_put(uint8_t *b) {
  if (!b) return;
  if (t_body_pool_n < HTTPNG_BODY_POOL_MAX) t_body_pool[t_body_pool_n++] = b;
  else free(b);
}

void httpng_body_pool_release(void) {
  while (t_body_pool_n) free(t_body_pool[--t_body_pool_n]);
}

#endif /* HAVE_HTTP2 || HAVE_HTTP3 */
