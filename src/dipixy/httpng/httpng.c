/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#if defined(HAVE_HTTP2) || defined(HAVE_HTTP3)

#include "httpng.h"

#include "../core/route.h"
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

void httpng_parse_known_header(const char *name, size_t namelen, const char *value, size_t valuelen, char *method, size_t method_sz, char *path, size_t path_sz,
                               char *inm, size_t inm_sz, char *origin, size_t origin_sz, char *authz, size_t authz_sz, char *protocol, size_t protocol_sz) {
  const uint8_t *v = (const uint8_t *)value;
  if (namelen == 7 && memcmp(name, ":method", 7) == 0) {
    hdr_value_copy(method, method_sz, v, valuelen);
  } else if (namelen == 5 && memcmp(name, ":path", 5) == 0) {
    hdr_value_copy(path, path_sz, v, valuelen);
  } else if (namelen == 13 && memcmp(name, "if-none-match", 13) == 0) {
    hdr_value_copy(inm, inm_sz, v, valuelen);
    strip_etag_quotes(inm);
  } else if (namelen == 6 && memcmp(name, "origin", 6) == 0) {
    hdr_value_copy(origin, origin_sz, v, valuelen);
  } else if (namelen == 13 && memcmp(name, "authorization", 13) == 0) {
    hdr_value_copy(authz, authz_sz, v, valuelen);
  } else if (namelen == 9 && memcmp(name, ":protocol", 9) == 0) {
    hdr_value_copy(protocol, protocol_sz, v, valuelen);
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

int httpng_hls_cold_render(hls_cold_kind_t kind, seg_container_t container, int want_ll, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc,
                           const char *filename, int is_head, hls_resp_t *out) {
  switch (kind) {
    case HLS_COLD_LLHLS:
      return hls_render_ll(ctx, filter, pmt_pid, lcevc, filename, is_head, NULL, out);
    case HLS_COLD_DASH:
      return dash_render(ctx, filter, pmt_pid, lcevc, want_ll, reactor_cfg()->dash_utc_url, is_head, out);
    case HLS_COLD_HLS:
    case HLS_COLD_MP4:
    default:
      return hls_render(ctx, filter, pmt_pid, lcevc, container, filename, is_head, NULL, out);
  }
}

typedef struct {
  const httpng_ops_t *ops;
  void *conn;
  void *req;
  route_t *rt;
  pid_filter_t *filter;
  unsigned pmt_pid;
  lcevc_select_t *lcevc;
  route_item_bufs_t *item_bufs;
  client_info_t *cinfo;
  const char *client_ip;
  int fd;
  int is_head;
  const char *origin;
  const char *inm;
  const char *query;
} httpng_req_t;

static void dispatch_ts_route(httpng_req_t *rq) {
  unsigned list_num;
  capture_ctx_t *ctx;
  int sub;
  if (!rq->ops->admission_ok(rq->conn)) {
    rq->ops->respond_status(rq->conn, rq->req, "503");
    return;
  }
  ctx = open_source(rq->rt, &list_num);
  if (!ctx) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  route_client_info(rq->rt, list_num, rq->filter, rq->rt->fmt == ROUTE_FMT_SPTS ? rq->pmt_pid : 0, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo);
  sub = ts_push_subscribe(ctx, rq->filter, rq->ops->proto, rq->fd, rq->rt->fmt == ROUTE_FMT_SPTS ? rq->pmt_pid : 0, rq->rt->fmt == ROUTE_FMT_SPTS, 0, rq->cinfo, rq->lcevc);
  if (sub < 0) {
    capture_close(ctx);
    rq->ops->respond_status(rq->conn, rq->req, "501");
    return;
  }
  if (!rq->ops->tspush_dispatch(rq->conn, rq->req, sub)) {
    ts_push_unsubscribe_by_idx(sub);
    rq->ops->respond_status(rq->conn, rq->req, "501");
  }
}

static void dispatch_hls_route(httpng_req_t *rq) {
  seg_container_t container = rq->rt->fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int handled;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   container, container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_501) {
    rq->ops->respond_status(rq->conn, rq->req, "501");
    return;
  }
  ctx = rs.ctx;
  if (!strcmp(rq->rt->hls_file, "index.m3u8") && !hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_HLS, container, 0, rq->is_head, rq->origin, (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle))
    return;
  handled = hls_render(ctx, rq->filter, rq->pmt_pid, rq->lcevc, container, rq->rt->hls_file, rq->is_head, rq->inm, &resp);
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

static void dispatch_llhls_route(httpng_req_t *rq) {
  uint32_t want_seg;
  int want_part;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int handled;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_TS,
                   reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_501) {
    rq->ops->respond_status(rq->conn, rq->req, "501");
    return;
  }
  ctx = rs.ctx;
  if (!strcmp(rq->rt->hls_file, "index_ll.m3u8") && !hls_ll_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_TS) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_LLHLS, SEG_CONTAINER_TS, 0, rq->is_head, rq->origin, (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle))
    return;
  if (!strcmp(rq->rt->hls_file, "index_ll.m3u8") && parse_blocking_reload(rq->query, &want_seg, &want_part) &&
      !hls_part_available(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_TS, want_seg, want_part) &&
      rq->ops->llhls_try_park(rq->conn, rq->req, ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->is_head, rq->inm, rq->origin, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), rs.ws_handle))
    return;
  handled = hls_render_ll(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->is_head, rq->inm, &resp);
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

static void dispatch_dash_route(httpng_req_t *rq) {
  int want_ll = rq->rt->fmt == ROUTE_FMT_LLDASH;
  hls_resp_t resp;
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  int handled;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   SEG_CONTAINER_FMP4, want_ll ? reactor_cfg()->dash_part_size : 0.0, &rs);
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_501) {
    rq->ops->respond_status(rq->conn, rq->req, "501");
    return;
  }
  ctx = rs.ctx;
  if (strcmp(rq->rt->hls_file, "manifest.mpd") != 0) {
    if (!reactor_cfg()->no_lldash && !rq->is_head) {
      int sub = dash_lldash_subscribe(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->ops->proto == 3 ? DASH_PROTO_H3 : DASH_PROTO_H2);
      if (sub >= 0) {
        if (rq->ops->dashchunk_dispatch(rq->conn, rq->req, sub, rs.ws_handle)) return;
        dash_lldash_sub_close(sub);
      }
    }
    handled = dash_render_seg(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, rq->is_head, &resp);
  } else {
    if (!hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_FMP4) &&
        rq->ops->hls_cold_try_park(rq->conn, rq->req, ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->rt->hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, rq->is_head, rq->origin, (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle))
      return;
    handled = dash_render(ctx, rq->filter, rq->pmt_pid, rq->lcevc, want_ll, reactor_cfg()->dash_utc_url, rq->is_head, &resp);
  }
  rq->ops->respond_hls(rq->conn, rq->req, handled, &resp, rq->origin);
  if (handled && resp.status == 200) ws_clients_add_bytes(rs.ws_handle, resp.body_len);
}

static void dispatch_mp4_route(httpng_req_t *rq) {
  route_setup_t rs;
  route_setup_status_t st;
  unsigned list_num;
  capture_ctx_t *ctx;
  st = route_setup(rq->rt, &list_num, rq->filter, rq->pmt_pid, rq->lcevc, rq->client_ip, rq->ops->proto, rq->item_bufs, rq->cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4, 0.0, &rs);
  if (st == ROUTE_SETUP_404) {
    rq->ops->respond_status(rq->conn, rq->req, "404");
    return;
  }
  if (st == ROUTE_SETUP_501) {
    rq->ops->respond_status(rq->conn, rq->req, "501");
    return;
  }
  ctx = rs.ctx;
  if (!hls_store_ready(ctx, rq->filter, rq->pmt_pid, rq->lcevc, SEG_CONTAINER_FMP4) &&
      rq->ops->hls_cold_try_park(rq->conn, rq->req, ctx, rq->filter, rq->pmt_pid, rq->lcevc, "", HLS_COLD_MP4, SEG_CONTAINER_FMP4, 0, rq->is_head, rq->origin, (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle))
    return;
  {
    int sub = mp4push_subscribe(ctx, rq->filter, rq->pmt_pid, rq->lcevc, rq->ops->proto);
    if (sub >= 0 && rq->ops->mp4push_dispatch(rq->conn, rq->req, sub, rs.ws_handle)) return;
    if (sub >= 0) mp4push_sub_close(sub);
  }
  rq->ops->respond_status(rq->conn, rq->req, "501");
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
  httpng_req_t rq;

  if (!strcmp(hdrs->method, "CONNECT") && !strcmp(hdrs->protocol, "websocket")) {
    if (reactor_cfg()->no_status || strcmp(hdrs->path, "/ui/ws/"))
      ops->respond_status(conn, req, "404");
    else if (!http_auth_ok(reactor_cfg(), hdrs->authz[0] ? hdrs->authz : NULL))
      ops->respond_401(conn, req);
    else
      ops->ws_dispatch(conn, req);
    return;
  }

  if (strcmp(hdrs->method, "GET") != 0 && strcmp(hdrs->method, "HEAD") != 0) {
    ops->respond_status(conn, req, "405");
    return;
  }
  qmark = strchr(hdrs->path, '?');
  query = qmark ? qmark + 1 : NULL;
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
  if (qmark) *qmark = '\0';
  if (route_parse(hdrs->path, &rt) || route_disabled(&rt)) {
    ops->respond_status(conn, req, "404");
    return;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS:
      dispatch_ts_route(&rq);
      return;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4:
      dispatch_hls_route(&rq);
      return;
    case ROUTE_FMT_LLHLS:
      dispatch_llhls_route(&rq);
      return;
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH:
      dispatch_dash_route(&rq);
      return;
    case ROUTE_FMT_MP4:
      dispatch_mp4_route(&rq);
      return;
    default:
      ops->respond_status(conn, req, "501");
      return;
  }
}

#endif /* HAVE_HTTP2 || HAVE_HTTP3 */
