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
    case 404: return "404";
    case 501: return "501";
    case 503: return "503";
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

void httpng_parse_known_header(const char *name, size_t namelen, const char *value, size_t valuelen, const httpng_hdr_out_t *out) {
  const uint8_t *v = (const uint8_t *)value;
  if (namelen == 7 && memcmp(name, ":method", 7) == 0) {
    hdr_value_copy(out->method, out->method_sz, v, valuelen);
  } else if (namelen == 5 && memcmp(name, ":path", 5) == 0) {
    hdr_value_copy(out->path, out->path_sz, v, valuelen);
  } else if (namelen == 13 && memcmp(name, "if-none-match", 13) == 0) {
    hdr_value_copy(out->inm, out->inm_sz, v, valuelen);
  } else if (namelen == 6 && memcmp(name, "origin", 6) == 0) {
    hdr_value_copy(out->origin, out->origin_sz, v, valuelen);
  } else if (namelen == 13 && memcmp(name, "authorization", 13) == 0) {
    hdr_value_copy(out->authz, out->authz_sz, v, valuelen);
  } else if (namelen == 9 && memcmp(name, ":protocol", 9) == 0) {
    hdr_value_copy(out->protocol, out->protocol_sz, v, valuelen);
  } else if ((namelen == 10 && memcmp(name, ":authority", 10) == 0) || (namelen == 4 && memcmp(name, "host", 4) == 0)) {
    hdr_value_copy(out->host, out->host_sz, v, valuelen);
  } else if (namelen == 10 && memcmp(name, "soapaction", 10) == 0) {
    hdr_value_copy(out->soapaction, out->soapaction_sz, v, valuelen);
  } else if (namelen == 3 && memcmp(name, "sid", 3) == 0) {
    hdr_value_copy(out->sid, out->sid_sz, v, valuelen);
  } else if (namelen == 8 && memcmp(name, "callback", 8) == 0) {
    hdr_value_copy(out->callback, out->callback_sz, v, valuelen);
  } else if (namelen == 2 && memcmp(name, "nt", 2) == 0) {
    hdr_value_copy(out->nt, out->nt_sz, v, valuelen);
  } else if (namelen == 7 && memcmp(name, "timeout", 7) == 0) {
    hdr_value_copy(out->timeout, out->timeout_sz, v, valuelen);
  }
}

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

int httpng_hls_cold_render(hls_cold_kind_t kind, seg_container_t container, int want_ll, const char *utc_url, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid,
                           const lcevc_select_t *lcevc, const char *filename, int is_head, hls_resp_t *out) {
  switch (kind) {
    case HLS_COLD_LLHLS:
      return hls_render_ll(ctx, filter, pmt_pid, lcevc, container, filename, is_head, NULL, out);
    case HLS_COLD_DASH:
      return dash_render(ctx, filter, pmt_pid, lcevc, want_ll, utc_url, is_head, out);
    case HLS_COLD_HLS:
    case HLS_COLD_MP4:
    default:
      return hls_render(ctx, filter, pmt_pid, lcevc, container, filename, is_head, NULL, out);
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
  rq->ops->respond_status(rq->conn, rq->req, "503");
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
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  res = open_source_try(rq->rt, &list_num, &ctx, &job);
  if (res == OPEN_RES_PENDING) {
    park_open(rq, job);
    return;
  }
  if (res == OPEN_RES_FAIL) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
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
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  if (!rq->ops->tspush_dispatch(rq->conn, rq->req, sub, rawaudio)) {
    ts_push_unsubscribe_by_idx(sub);
    rq->ops->respond_status(rq->conn, rq->req, "503");
  }
}

void dispatch_hls_route(httpng_req_t *rq) {
  seg_container_t container = rq->rt->fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int handled;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   container, container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_PENDING) {
    park_open(rq, rs.job);
    return;
  }
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_503) {
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  ctx = rs.ctx;
  if (!strcmp(rq->rt->hls_file, "index.m3u8") && !hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, &(hls_cold_park_req_t){ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_HLS, container, 0, rq->is_head, 0, rq->origin, hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, NULL}))
    return;
  handled = hls_render(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container, rq->rt->hls_file, rq->is_head, rq->inm, &resp);
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

void dispatch_llhls_route(httpng_req_t *rq) {
  seg_container_t container = rq->rt->fmt == ROUTE_FMT_LLHLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  uint32_t want_seg;
  int want_part;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int handled;
  int is_index_ll;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count, container,
    reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_PENDING) {
    park_open(rq, rs.job);
    return;
  }
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_503) {
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  ctx = rs.ctx;
  is_index_ll = !strcmp(rq->rt->hls_file, "index_ll.m3u8");
  if (is_index_ll && !hls_ll_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, &(hls_cold_park_req_t){ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_LLHLS, container, 0, rq->is_head, 0, rq->origin, hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, NULL}))
    return;
  if (is_index_ll && parse_blocking_reload(rq->query, &want_seg, &want_part) &&
      !hls_part_available(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container, want_seg, want_part) &&
      rq->ops->llhls_try_park(rq->conn, rq->req, &(llhls_park_req_t){ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->is_head, 0, rq->inm, rq->origin, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), rs.ws_handle, container}))
    return;
  handled = hls_render_ll(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container, rq->rt->hls_file, rq->is_head, rq->inm, &resp);
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

void dispatch_dash_route(httpng_req_t *rq) {
  int want_ll = rq->rt->fmt == ROUTE_FMT_LLDASH;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  char utc_url[TIME_EP_URL_MAX];
  int handled;
  time_ep_utc_url(utc_url, sizeof utc_url, reactor_cfg()->dash_utc_url, 1, rq->host);
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   SEG_CONTAINER_FMP4, want_ll ? reactor_cfg()->dash_part_size : 0.0, &rs);
  if (st == ROUTE_SETUP_PENDING) {
    park_open(rq, rs.job);
    return;
  }
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_503) {
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  ctx = rs.ctx;
  if (strcmp(rq->rt->hls_file, "manifest.mpd") != 0) {
    if (!reactor_cfg()->no_lldash && !rq->is_head) {
      int sub = dash_lldash_subscribe(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->ops->proto);
      if (sub >= 0) {
        if (rq->ops->dashchunk_dispatch(rq->conn, rq->req, sub, rs.ws_handle)) return;
        dash_lldash_sub_close(sub);
      }
    }
    handled = dash_render_seg(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->is_head, &resp);
  } else {
    if (!hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_FMP4) &&
        rq->ops->hls_cold_try_park(rq->conn, rq->req, &(hls_cold_park_req_t){ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, rq->is_head, 0, rq->origin, hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, utc_url}))
      return;
    handled = dash_render(ctx, rq->filter, rq->pmt_pid, rq->lcevc, want_ll, utc_url, rq->is_head, &resp);
  }
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

void dispatch_mp4_route(httpng_req_t *rq) {
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int sub;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4, 0.0, &rs);
  if (st == ROUTE_SETUP_PENDING) {
    park_open(rq, rs.job);
    return;
  }
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_503) {
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  ctx = rs.ctx;
  if (!hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_FMP4) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, &(hls_cold_park_req_t){ctx, rq->filter, rq->pmt_pid, rq->lcevc, "", HLS_COLD_MP4, SEG_CONTAINER_FMP4, 0, rq->is_head, 0, rq->origin, hls_cold_timeout_ms(reactor_cfg()->segment_size), rs.ws_handle, NULL}))
    return;
  sub = mp4push_subscribe(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->ops->proto);
  if (sub >= 0 && rq->ops->mp4push_dispatch(rq->conn, rq->req, sub, rs.ws_handle)) return;
  if (sub >= 0) mp4push_sub_close(sub);
  rq->ops->respond_status(rq->conn, rq->req, "503");
}

static int method_is_dlna(const char *method) {
  return !strcmp(method, "POST") || !strcmp(method, "SUBSCRIBE") || !strcmp(method, "UNSUBSCRIBE");
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

  if (!t_open_replay) dipixy_metrics_note_request();
  if (!strcmp(hdrs->method, "CONNECT") && !strcmp(hdrs->protocol, "websocket")) {
    if (reactor_cfg()->no_status || !path_is_ui_ws(hdrs->path))
      ops->respond_status(conn, req, "404");
    else if (!ws_origin_ok(reactor_cfg(), hdrs->origin, hdrs->host))
      ops->respond_status(conn, req, "403");
    else if (!http_auth_ok(reactor_cfg()->http_auth, hdrs->authz[0] ? hdrs->authz : NULL))
      ops->respond_401(conn, req);
    else
      ops->ws_dispatch(conn, req);
    return;
  }

  qmark = strchr(hdrs->path, '?');
  query = qmark ? qmark + 1 : NULL;
  if (qmark) *qmark = '\0';

  if (method_is_dlna(hdrs->method)) {
    httpng_dispatch_dlna(ops, conn, req, hdrs, client_ip);
    return;
  }
  if (strcmp(hdrs->method, "GET") != 0 && strcmp(hdrs->method, "HEAD") != 0) {
    ops->respond_status(conn, req, "405");
    return;
  }
  want_auth = special_path_auth(hdrs->path);
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
  rq.is_head = !strcmp(hdrs->method, "HEAD");
  rq.origin = hdrs->origin[0] ? hdrs->origin : NULL;
  rq.inm = hdrs->inm[0] ? hdrs->inm : NULL;
  rq.query = query;
  rq.host = hdrs->host;
  rq.opening = 0;
  if (httpng_dispatch_special(ops, conn, req, hdrs, query, &filter, &lcevc, rq.is_head)) return;
  if (route_parse(hdrs->path, &rt) || route_disabled(&rt)) {
    ops->respond_status(conn, req, "404");
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
      dispatch_hls_route(&rq);
      break;
    case ROUTE_FMT_LLHLS:
    case ROUTE_FMT_LLHLS_FMP4:
      dispatch_llhls_route(&rq);
      break;
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH:
      dispatch_dash_route(&rq);
      break;
    case ROUTE_FMT_MP4:
      dispatch_mp4_route(&rq);
      break;
    default:
      ops->respond_status(conn, req, "501");
      break;
  }
  if (rq.opening && qmark) *qmark = '?';
}

#endif /* HAVE_HTTP2 || HAVE_HTTP3 */
