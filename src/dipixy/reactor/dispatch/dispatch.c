/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HTTP/1.1 request parsing and dispatch */

#define _DEFAULT_SOURCE
#include "priv.h"

#include "route_common.h"

#include "../../core/metrics.h"
#include "../../hls/hls.h"
#include "../../dash/dash.h"
#include "../../dash/lldash.h"
#include "../../segment/segment.h"
#include "../../segment/mp4push.h"
#include "../../ts/lcevcselect.h"
#include "../../ts/pmtselect.h"
#include "../../ts/ts_push.h"

#include "lib/helper/ioutil.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define REACTOR_MAX_REQ 8192

typedef enum { HTTP_METHOD_GET, HTTP_METHOD_HEAD, HTTP_METHOD_POST, HTTP_METHOD_SUBSCRIBE, HTTP_METHOD_UNSUBSCRIBE, HTTP_METHOD_OTHER } http_method_t;

static http_method_t parse_http_method(const char *method, size_t method_len) {
  if (method_len == 3 && !memcmp(method, "GET", 3)) return HTTP_METHOD_GET;
  if (method_len == 4 && !memcmp(method, "HEAD", 4)) return HTTP_METHOD_HEAD;
  if (method_len == 4 && !memcmp(method, "POST", 4)) return HTTP_METHOD_POST;
  if (method_len == 9 && !memcmp(method, "SUBSCRIBE", 9)) return HTTP_METHOD_SUBSCRIBE;
  if (method_len == 11 && !memcmp(method, "UNSUBSCRIBE", 11)) return HTTP_METHOD_UNSUBSCRIBE;
  return HTTP_METHOD_OTHER;
}

static void dispatch_dlna_subprotocol(int epfd, conn_t *c, const char *path, http_method_t method_kind, const struct phr_header *headers, size_t num_headers, int keep_alive) {
  if (!reactor_cfg()->enable_dlna) {
    respond_status(c, RESP_404, keep_alive);
  } else {
    switch (method_kind) {
      case HTTP_METHOD_POST:
        if (!strcmp(path, "/dlna/cd_control") || !strcmp(path, "/dlna/cm_control")) {
          char clbuf[16];
          size_t body_len = 0;
          if (find_header(headers, num_headers, "Content-Length", clbuf, sizeof clbuf)) {
            char *end;
            unsigned long cl = strtoul(clbuf, &end, 10);
            if (*end == '\0') body_len = (size_t)cl;
          }
          serve_dlna_control(c, !strcmp(path, "/dlna/cd_control") ? "cd" : "cm", headers, num_headers, (char *)c->in.buf + c->req_bytes, body_len, keep_alive);
          c->req_bytes += body_len;
        } else {
          respond_status(c, RESP_405, keep_alive);
        }
        break;
      case HTTP_METHOD_SUBSCRIBE:
        if (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event")) serve_dlna_subscribe(c, headers, num_headers, keep_alive);
        else respond_status(c, RESP_405, keep_alive);
        break;
      case HTTP_METHOD_UNSUBSCRIBE:
        if (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event")) serve_dlna_unsubscribe(c, keep_alive);
        else respond_status(c, RESP_405, keep_alive);
        break;
      case HTTP_METHOD_GET:
      case HTTP_METHOD_HEAD:
      case HTTP_METHOD_OTHER:
        respond_status(c, RESP_405, keep_alive);
        break;
    }
  }
  c->state = CONN_WRITING;
  reactor_finish(epfd, c);
}

/* desc.xml/scpd/metrics/index/status.js/ws-upgrade/export: 1 = handled (caller finishes), 0 = fall through to route dispatch */
static int dispatch_special_path(conn_t *c, const char *path, int is_head, const struct phr_header *headers, size_t num_headers, int keep_alive, const pid_filter_t *filter, const lcevc_select_t *lcevc, const char *query) {
  if (reactor_cfg()->enable_dlna) {
    static const struct { const char *path; void (*fn)(conn_t *, int, int); } dlna_paths[] = {
      {"/dlna/desc.xml", serve_dlna_desc}, {"/dlna/cd_scpd.xml", serve_dlna_cd_scpd}, {"/dlna/cm_scpd.xml", serve_dlna_cm_scpd},
    };
    for (size_t i = 0; i < sizeof dlna_paths / sizeof dlna_paths[0]; i++)
      if (!strcmp(path, dlna_paths[i].path)) {
        dlna_paths[i].fn(c, is_head, keep_alive);
        return 1;
      }
  }

  if (reactor_cfg()->metrics_http && !strcmp(path, "/metrics")) {
    serve_metrics(c, is_head, keep_alive);
    return 1;
  }
  if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
    if (reactor_cfg()->no_status)
      respond_status(c, RESP_404, keep_alive);
    else
      serve_htdocs_index(c, is_head, keep_alive);
    return 1;
  }
  if (!reactor_cfg()->no_status && !strcmp(path, "/ui/status.js")) {
    serve_status(c, is_head, keep_alive);
    return 1;
  }
  if (!reactor_cfg()->no_status && !is_head && ws_try_upgrade(c, path, headers, num_headers))
    return 1;

  if (!strncmp(path, "/export/", 8)) {
    route_fmt_t exp_fmt;
    playlist_type_t exp_ptype;
    char host_buf[128];
    const char *host_hdr = find_header(headers, num_headers, "Host", host_buf, sizeof host_buf) ? host_buf : NULL;
    if (playlist_path_parse(path, &exp_fmt, &exp_ptype) || playlist_fmt_disabled(reactor_cfg(), exp_fmt)) {
      respond_status(c, RESP_404, keep_alive);
      return 1;
    }
    serve_playlist(c, exp_fmt, exp_ptype, host_hdr, query, filter, lcevc, is_head, keep_alive);
    return 1;
  }
  return 0;
}

typedef enum { ROUTE_RESULT_FINISH, ROUTE_RESULT_PARKED } route_result_t;

static route_result_t dispatch_route_ts(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  char header[160];
  size_t header_len;
  unsigned tp_pmt_pid = rt->fmt == ROUTE_FMT_TS ? 0 : pmt_pid;
  int spts = rt->fmt == ROUTE_FMT_SPTS;
  int rawaudio = rt->fmt == ROUTE_FMT_RAWAUDIO;
  capture_ctx_t *ctx = open_source(rt, &list_num);
  int sub;
  if (!ctx) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  header_len = build_stream_header(header, sizeof header, rawaudio ? "audio/mpeg" : "video/mp2t", c->ssl != NULL);
  if (is_head) {
    capture_close(ctx);
    conn_queue(c, header, header_len);
    return ROUTE_RESULT_FINISH;
  }
  if (!capture_ctx_bytes(ctx) && reactor_cfg()->ts_startup_timeout_s > 0.0) {
    ts_cold_park_req_t req = {ctx, rt, list_num, filter, tp_pmt_pid, lcevc, spts, rawaudio, keep_alive, (int)(reactor_cfg()->ts_startup_timeout_s * 1000.0)};
    if (ts_cold_try_park(c, &req)) return ROUTE_RESULT_PARKED;
  }
  route_client_info(rt, list_num, filter, tp_pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
  sub = ts_push_subscribe(ctx, filter, CONN_PROTO_H1, c->fd, tp_pmt_pid, spts, rawaudio, &cinfo, lcevc);
  if (sub < 0) {
    capture_close(ctx);
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  c->slot = sub;
  conn_queue(c, header, header_len);
  c->next_state = CONN_NEXT_TSPUSH;
  return ROUTE_RESULT_FINISH;
}

static route_result_t dispatch_route_hls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid,
  const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  seg_container_t container = rt->fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
    container, container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_501) {
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!strcmp(rt->hls_file, "index.m3u8") && !hls_store_ready(rs.ctx, filter, pmt_pid, lcevc, container) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_HLS, container, 0, is_head, keep_alive, origin_hdr,
        (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (hls_serve(c, rs.ctx, filter, pmt_pid, lcevc, container, rt->hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes))
    ws_clients_add_bytes(rs.ws_handle, bytes);
  else
    respond_status(c, RESP_404, keep_alive);
  return ROUTE_RESULT_FINISH;
}

static route_result_t dispatch_route_llhls(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive,
  const char *if_none_match, const char *origin_hdr, const char *query) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  uint32_t want_seg;
  int want_part;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
    SEG_CONTAINER_TS, reactor_cfg()->hls_part_size, &rs);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_501) {
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!strcmp(rt->hls_file, "index_ll.m3u8") && !hls_ll_store_ready(rs.ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_TS) && hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_LLHLS, SEG_CONTAINER_TS, 0, is_head, keep_alive, origin_hdr,
      (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (!strcmp(rt->hls_file, "index_ll.m3u8") && parse_blocking_reload(query, &want_seg, &want_part) &&
      !hls_part_available(rs.ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_TS, want_seg, want_part) &&
      llhls_try_park(c, &(llhls_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, is_head, keep_alive, NULL, origin_hdr, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), rs.ws_handle})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (hls_serve_ll(c, rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes)) {
    ws_clients_add_bytes(rs.ws_handle, bytes);
  } else {
    respond_status(c, RESP_404, keep_alive);
  }
  return ROUTE_RESULT_FINISH;
}

static route_result_t dispatch_route_dash(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  int want_ll = rt->fmt == ROUTE_FMT_LLDASH;
  route_setup_t rs;
  route_setup_status_t st;
  size_t bytes = 0;
  st = route_setup(rt, &list_num, filter, pmt_pid, lcevc, c->client_ip, 1, &item_bufs, &cinfo, reactor_cfg()->segment_size, reactor_cfg()->segment_count,
                   SEG_CONTAINER_FMP4, want_ll ? reactor_cfg()->dash_part_size : 0.0, &rs);
  if (st == ROUTE_SETUP_404) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (st == ROUTE_SETUP_501) {
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (strcmp(rt->hls_file, "manifest.mpd") != 0) {
    if (!reactor_cfg()->no_lldash && !is_head && dash_lldash_try_attach(c, rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, keep_alive, origin_hdr, rs.ws_handle))
      return ROUTE_RESULT_FINISH;
    if (dash_serve_seg(c, rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, is_head, keep_alive, origin_hdr, &bytes))
      ws_clients_add_bytes(rs.ws_handle, bytes);
    else
      respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_store_ready(rs.ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){rs.ctx, filter, pmt_pid, lcevc, rt->hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, is_head, keep_alive, origin_hdr, (int)(reactor_cfg()->segment_size * 2000.0), rs.ws_handle})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (dash_serve(c, rs.ctx, filter, pmt_pid, lcevc, want_ll, reactor_cfg()->dash_utc_url, is_head, keep_alive, origin_hdr, &bytes))
    ws_clients_add_bytes(rs.ws_handle, bytes);
  else
    respond_status(c, RESP_404, keep_alive);
  return ROUTE_RESULT_FINISH;
}

static route_result_t dispatch_route_mp4(conn_t *c, const route_t *rt, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int is_head, int keep_alive, const char *origin_hdr) {
  unsigned list_num;
  client_info_t cinfo;
  route_item_bufs_t item_bufs;
  char mp4_head_header[160];
  size_t mp4_head_len = build_stream_header(mp4_head_header, sizeof mp4_head_header, "video/mp4", c->ssl != NULL);
  int wsh;
  capture_ctx_t *ctx = open_source(rt, &list_num);
  if (!ctx) {
    respond_status(c, RESP_404, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (is_head) {
    capture_close(ctx);
    conn_queue(c, mp4_head_header, mp4_head_len);
    return ROUTE_RESULT_FINISH;
  }
  route_client_info(rt, list_num, filter, pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
  wsh = ws_clients_touch(&cinfo);
  if (wsh < 0) {
    capture_close(ctx);
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_seg_touch(ctx, filter, pmt_pid, lcevc, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4, 0.0)) {
    respond_status(c, RESP_501, keep_alive);
    return ROUTE_RESULT_FINISH;
  }
  if (!hls_store_ready(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4) &&
      hls_cold_try_park(c, &(hls_cold_park_req_t){ctx, filter, pmt_pid, lcevc, "", HLS_COLD_MP4, SEG_CONTAINER_FMP4, 0, is_head, keep_alive, origin_hdr, (int)(reactor_cfg()->segment_size * 2000.0), wsh})) {
    c->state = CONN_DISPATCH;
    return ROUTE_RESULT_PARKED;
  }
  if (!mp4push_try_attach(c, ctx, filter, pmt_pid, lcevc, wsh)) {
    respond_status(c, RESP_501, keep_alive);
  }
  return ROUTE_RESULT_FINISH;
}

/* dispatche request from c->in: parse reqln */
static void reactor_dispatch(int epfd, conn_t *c, const char *method, size_t method_len, const char *path_in, size_t path_len, int minor_version, const struct phr_header *headers, size_t num_headers, size_t header_bytes) {
  char *path;
  char *qmark;
  const char *query;
  char inm_buf[80];
  char origin_buf[128];
  const char *if_none_match;
  const char *origin_hdr;
  route_t rt;
  pid_filter_t filter;
  lcevc_select_t lcevc;
  unsigned pmt_pid;
  route_result_t rr;
  http_method_t method_kind;
  int is_head;
  int keep_alive;
  const char *want_auth;
  c->req_bytes = header_bytes;
  dipixy_metrics_note_request();
  path = (char *)path_in;
  path[path_len] = '\0';
  keep_alive = wants_keepalive(minor_version, headers, num_headers);
  if_none_match = NULL;
  if (find_header(headers, num_headers, "If-None-Match", inm_buf, sizeof inm_buf)) {
    strip_etag_quotes(inm_buf);
    if_none_match = inm_buf;
  }
  origin_hdr = find_header(headers, num_headers, "Origin", origin_buf, sizeof origin_buf) ? origin_buf : NULL;
  method_kind = parse_http_method(method, method_len);
  is_head = method_kind == HTTP_METHOD_HEAD;
  if (method_kind == HTTP_METHOD_OTHER) {
    respond_status(c, RESP_405, keep_alive);
    goto finish;
  }
  qmark = strchr(path, '?');
  query = NULL;
  if (qmark) {
    *qmark = '\0';
    query = qmark + 1;
  }

  if (method_kind == HTTP_METHOD_POST || method_kind == HTTP_METHOD_SUBSCRIBE || method_kind == HTTP_METHOD_UNSUBSCRIBE) {
    dispatch_dlna_subprotocol(epfd, c, path, method_kind, headers, num_headers, keep_alive);
    return;
  }

  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters) pid_filter_parse_query(query, &filter);
  pmt_pid = pmt_select_parse_query(query);
  lcevc_select_parse_query(reactor_cfg()->no_lcevc ? NULL : query, &lcevc);
  want_auth = NULL;
  if (!strcmp(path, "/") || !strcmp(path, "/index.html") || !strcmp(path, "/ui/status.js") || !strcmp(path, "/ui/ws/") || !strcmp(path, "/ui/ws")) {
    if (reactor_cfg()->http_auth[0]) want_auth = reactor_cfg()->http_auth;
  } else if (!strcmp(path, "/metrics")) {
    if (reactor_cfg()->http_metrics_auth[0]) want_auth = reactor_cfg()->http_metrics_auth;
  }
  if (want_auth) {
    char authz_buf[200];
    const char *authz = find_header(headers, num_headers, "Authorization", authz_buf, sizeof authz_buf) ? authz_buf : NULL;
    if (!http_auth_ok(want_auth, authz)) {
      respond_401(c, keep_alive);
      goto finish;
    }
  }

  if (dispatch_special_path(c, path, is_head, headers, num_headers, keep_alive, &filter, &lcevc, query))
    goto finish;

  if (route_parse(path, &rt) || route_disabled(&rt)) {
    respond_status(c, RESP_404, keep_alive);
    goto finish;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS:
    case ROUTE_FMT_RAWAUDIO:
      rr = dispatch_route_ts(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive);
      break;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4:
      rr = dispatch_route_hls(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, if_none_match, origin_hdr);
      break;
    case ROUTE_FMT_LLHLS:
      rr = dispatch_route_llhls(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, if_none_match, origin_hdr, query);
      break;
    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH:
      rr = dispatch_route_dash(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, origin_hdr);
      break;
    case ROUTE_FMT_MP4:
      rr = dispatch_route_mp4(c, &rt, &filter, pmt_pid, &lcevc, is_head, keep_alive, origin_hdr);
      break;
    default:
      respond_status(c, RESP_501, keep_alive);
      rr = ROUTE_RESULT_FINISH;
      break;
  }
  if (rr == ROUTE_RESULT_PARKED) return;

finish:
  c->state = CONN_WRITING;
  reactor_finish(epfd, c);
}

#define REACTOR_MAX_HEADERS 100

void reactor_read(int epfd, conn_t *c) {
  const char *method, *path;
  size_t method_len, path_len;
  int minor_version;
  struct phr_header headers[REACTOR_MAX_HEADERS];
  size_t num_headers;
  int pret;
  char clbuf[16];
  for (;;) {
    size_t room;
    ssize_t n;
    if (c->in.len >= REACTOR_MAX_REQ - 1) {
      respond_status(c, RESP_431, 0);
      c->state = CONN_WRITING;
      reactor_finish(epfd, c);
      return;
    }
    if (conn_in_reserve(c, 4096 + 1) < 0) { /* +1: room for NUL after fill */
      reactor_close(epfd, c);
      return;
    }
    room = c->in.cap - c->in.len - 1;
    n = tls_net_recv(c->fd, c->in.buf + c->in.len, room);
    if (n > 0) {
      c->in.len += (size_t)n;
      continue;
    }
    if (n == 0) {
      reactor_close(epfd, c);
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    reactor_close(epfd, c);
    return;
  }

  c->in.buf[c->in.len] = '\0';
  num_headers = REACTOR_MAX_HEADERS;
  pret = phr_parse_request((const char *)c->in.buf, c->in.len, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, 0);
  if (pret == -2) {
    reactor_arm(epfd, c, t_tls_want_write);
    return;
  }
  if (pret == -1) {
    respond_status(c, RESP_400, 0);
    c->state = CONN_WRITING;
    reactor_finish(epfd, c);
    return;
  }
  if (find_header(headers, num_headers, "Content-Length", clbuf, sizeof clbuf)) {
    char *end;
    unsigned long cl = strtoul(clbuf, &end, 10);
    if (*end == '\0' && cl > 0) {
      size_t need = (size_t)pret + (size_t)cl;
      if (need > REACTOR_MAX_REQ - 1) {
        respond_status(c, RESP_400, 0);
        c->state = CONN_WRITING;
        reactor_finish(epfd, c);
        return;
      }
      if (c->in.len < need) {
        reactor_arm(epfd, c, t_tls_want_write);
        return;
      }
    }
  }
  reactor_dispatch(epfd, c, method, method_len, path, path_len, minor_version, headers, num_headers, (size_t)pret);
}

void reactor_keepalive(int epfd, conn_t *c) {
  size_t leftover = c->in.len > c->req_bytes ? c->in.len - c->req_bytes : 0;
  if (leftover) memmove(c->in.buf, c->in.buf + c->req_bytes, leftover);
  c->in.off = 0;
  c->in.len = leftover;
  c->out.off = 0;
  c->out.len = 0;
  c->keep_alive = 0;
  c->state = CONN_READING;
  reactor_arm(epfd, c, 0);
  if (leftover) reactor_read(epfd, c); /* pipelined buffered B, dispatch without waiting for other sock reads */
}
