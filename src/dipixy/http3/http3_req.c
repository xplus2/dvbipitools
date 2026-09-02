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
#include "../ts/pidfilter.h"
#include "../ts/pmtselect.h"
#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../core/route.h"
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
  if (!r)
    r = alloc_req(c, sid);
  return r ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
}

static int cb_h3_recv_header(nghttp3_conn *h3, int64_t sid, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value, uint8_t flags, void *ud, void *stream_ud) {
  (void)h3;
  (void)token;
  (void)flags;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r)
    return 0;

  nghttp3_vec n = nghttp3_rcbuf_get_buf(name);
  nghttp3_vec v = nghttp3_rcbuf_get_buf(value);

  if (n.len == 7 && memcmp(n.base, ":method", 7) == 0) {
    size_t cp = v.len < sizeof(r->method) - 1 ? v.len : sizeof(r->method) - 1;
    memcpy(r->method, v.base, cp);
    r->method[cp] = '\0';
  } else if (n.len == 5 && memcmp(n.base, ":path", 5) == 0) {
    size_t cp = v.len < sizeof(r->path) - 1 ? v.len : sizeof(r->path) - 1;
    memcpy(r->path, v.base, cp);
    r->path[cp] = '\0';
  } else if (n.len == 13 && memcmp(n.base, "if-none-match", 13) == 0) {
    size_t cp = v.len < sizeof(r->inm) - 1 ? v.len : sizeof(r->inm) - 1;
    memcpy(r->inm, v.base, cp);
    r->inm[cp] = '\0';
    strip_etag_quotes(r->inm);
  } else if (n.len == 6 && memcmp(n.base, "origin", 6) == 0) {
    size_t cp = v.len < sizeof(r->origin) - 1 ? v.len : sizeof(r->origin) - 1;
    memcpy(r->origin, v.base, cp);
    r->origin[cp] = '\0';
  } else if (n.len == 13 && memcmp(n.base, "authorization", 13) == 0) {
    size_t cp = v.len < sizeof(r->authz) - 1 ? v.len : sizeof(r->authz) - 1;
    memcpy(r->authz, v.base, cp);
    r->authz[cp] = '\0';
  } else if (n.len == 9 && memcmp(n.base, ":protocol", 9) == 0) {
    size_t cp = v.len < sizeof(r->protocol) - 1 ? v.len : sizeof(r->protocol) - 1;
    memcpy(r->protocol, v.base, cp);
    r->protocol[cp] = '\0';
  }
  return 0;
}

static int cb_h3_recv_data(nghttp3_conn *h3, int64_t sid, const uint8_t *data, size_t datalen, void *ud, void *stream_ud) {
  (void)h3;
  (void)sid;
  (void)ud;
  if (stream_ud)
    h3_ws_data_chunk(stream_ud, data, datalen);
  return 0;
}

static int cb_h3_end_headers(nghttp3_conn *h3, int64_t sid, int fin, void *ud, void *stream_ud) {
  (void)h3;
  (void)fin;
  (void)stream_ud;
  h3_conn_t *c = ud;
  h3_req_t *r = find_req(c, sid);
  if (!r)
    return 0;
  if (!r->dispatched)
    r->dispatch_pending = 1;
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

void dispatch_req(h3_conn_t *c, h3_req_t *r) {
  route_t rt;
  pid_filter_t filter;
  unsigned pmt_pid;
  unsigned list_num;
  route_item_bufs_t item_bufs;
  char client_ip[64];
  client_info_t cinfo;
  char *qmark, *query;
  int is_head;
  capture_ctx_t *ctx;
  if (r->dispatched) return;
  r->dispatched = 1;
  if (!strcmp(r->method, "CONNECT") && !strcmp(r->protocol, "websocket")) {
    if (reactor_cfg()->no_status || strcmp(r->path, "/ui/ws/"))
      h3_respond_status(c, r->stream_id, "404");
    else if (!http_auth_ok(reactor_cfg(), r->authz[0] ? r->authz : NULL))
      h3_respond_401(c, r->stream_id);
    else
      h3_ws_dispatch(c, r);
    return;
  }

  if (strcmp(r->method, "GET") != 0 && strcmp(r->method, "HEAD") != 0) {
    h3_respond_status(c, r->stream_id, "405");
    return;
  }
  is_head = !strcmp(r->method, "HEAD");
  qmark = strchr(r->path, '?');
  query = qmark ? qmark + 1 : NULL;
  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters)
    pid_filter_parse_query(query, &filter);
  pmt_pid = pmt_select_parse_query(query);
  if (qmark) *qmark = '\0';
  if (route_parse(r->path, &rt) || route_disabled(&rt)) {
    h3_respond_status(c, r->stream_id, "404");
    return;
  }
  h3_client_ip(c, client_ip, sizeof client_ip);

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS: {
      int sub;
      if (h3_conn_active_count(c) > H3_MAX_REQS - H3_WS_RESERVE) {
        h3_respond_status(c, r->stream_id, "503");
        return;
      }
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h3_respond_status(c, r->stream_id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, rt.fmt == ROUTE_FMT_SPTS ? pmt_pid : 0, client_ip, 3, &item_bufs, &cinfo);
      sub = ts_push_subscribe(ctx, &filter, 3, -1, rt.fmt == ROUTE_FMT_SPTS ? pmt_pid : 0, rt.fmt == ROUTE_FMT_SPTS, 0, &cinfo);
      if (sub < 0) {
        capture_close(ctx);
        h3_respond_status(c, r->stream_id, "501");
        return;
      }

      g_ts_subs[sub].h3c = c;
      g_ts_subs[sub].h3_sid = r->stream_id;
      ts_push_set_reactor_tid(sub, t_reactor_tid);
      r->tspush_sub_idx = sub;

      {
        nghttp3_nv nva[] = {
            {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE},
            {(uint8_t *)"content-type", (uint8_t *)"video/mp2t", 12, 10, NGHTTP3_NV_FLAG_NONE},
        };
        nghttp3_data_reader dr;
        dr.read_data = h3_tspush_read_cb;
        nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
        nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, 2, &dr);
      }
      atomic_store_explicit(&g_ts_subs[sub].ready, 1, memory_order_release);
      /* r stays alive, freed on stream close via cb_stream_close -> free_req */
      return;
    }

    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4: {
      seg_container_t container = rt.fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h3_respond_status(c, r->stream_id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, client_ip, 3, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, container,
                          container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size)) {
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (!strcmp(rt.hls_file, "index.m3u8") && !hls_store_ready(ctx, &filter, pmt_pid, container) &&
          h3_hls_cold_try_park(c, r->stream_id, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_HLS, container, 0, is_head,
                                r->origin[0] ? r->origin : NULL, (int)(reactor_cfg()->segment_size * 2000.0), wsh))
        return;
      handled = hls_render(ctx, &filter, pmt_pid, container, rt.hls_file, is_head, r->inm[0] ? r->inm : NULL, &resp);
      h3_respond_hls(c, r, handled, &resp);
      if (handled && resp.status == 200)
        ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    case ROUTE_FMT_LLHLS: {
      uint32_t want_seg;
      int want_part;
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h3_respond_status(c, r->stream_id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, client_ip, 3, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_TS, reactor_cfg()->hls_part_size)) {
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (!strcmp(rt.hls_file, "index_ll.m3u8") && !hls_ll_store_ready(ctx, &filter, pmt_pid, SEG_CONTAINER_TS) &&
          h3_hls_cold_try_park(c, r->stream_id, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_LLHLS, SEG_CONTAINER_TS, 0, is_head,
                                r->origin[0] ? r->origin : NULL, (int)(reactor_cfg()->segment_size * 2000.0), wsh))
        return;
      if (!strcmp(rt.hls_file, "index_ll.m3u8") && parse_blocking_reload(query, &want_seg, &want_part) &&
          !hls_part_available(ctx, &filter, pmt_pid, SEG_CONTAINER_TS, want_seg, want_part) &&
          h3_llhls_try_park(c, r->stream_id, ctx, &filter, pmt_pid, rt.hls_file, is_head, r->inm[0] ? r->inm : NULL,
                             r->origin[0] ? r->origin : NULL, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), wsh))
        return;
      handled = hls_render_ll(ctx, &filter, pmt_pid, rt.hls_file, is_head, r->inm[0] ? r->inm : NULL, &resp);
      h3_respond_hls(c, r, handled, &resp);
      if (handled && resp.status == 200)
        ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH: {
      int want_ll = rt.fmt == ROUTE_FMT_LLDASH;
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h3_respond_status(c, r->stream_id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, client_ip, 3, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4,
                          want_ll ? reactor_cfg()->dash_part_size : 0.0)) {
        h3_respond_status(c, r->stream_id, "501");
        return;
      }
      if (strcmp(rt.hls_file, "manifest.mpd") != 0) {
        if (!reactor_cfg()->no_lldash && !is_head) {
          int sub = dash_lldash_subscribe(ctx, &filter, pmt_pid, rt.hls_file, 3);
          if (sub >= 0) {
            nghttp3_nv nva[] = {
                {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE},
                {(uint8_t *)"content-type", (uint8_t *)"video/mp4", 12, 9, NGHTTP3_NV_FLAG_NONE},
            };
            nghttp3_data_reader dr;
            dr.read_data = h3_dashchunk_read_cb;
            r->dashchunk_sub_idx = sub;
            dash_lldash_h3_bind(sub, c, r->stream_id, t_reactor_tid, wsh);
            nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
            nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, 2, &dr);
            return;
          }
        }
        handled = dash_render_seg(ctx, &filter, pmt_pid, rt.hls_file, is_head, &resp);
      } else {
        if (!hls_store_ready(ctx, &filter, pmt_pid, SEG_CONTAINER_FMP4) &&
            h3_hls_cold_try_park(c, r->stream_id, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, is_head,
                                 r->origin[0] ? r->origin : NULL, (int)(reactor_cfg()->segment_size * 2000.0), wsh))
          return;
        handled = dash_render(ctx, &filter, pmt_pid, want_ll, reactor_cfg()->dash_utc_url, is_head, &resp);
      }
      h3_respond_hls(c, r, handled, &resp);
      if (handled && resp.status == 200) ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    default:
      h3_respond_status(c, r->stream_id, "501");
      return;
  }
}

#endif /* HAVE_HTTP3 */
