/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HTTP/1.1 request parsing and dispatch */

#define _DEFAULT_SOURCE
#include "priv.h"

#include "../../core/metrics.h"
#include "../../hls/hls.h"
#include "../../dash/dash.h"
#include "../../dash/lldash.h"
#include "../../segment/segment.h"
#include "../../ts/pmtselect.h"
#include "../../ts/ts_push.h"

#include "lib/helper/ioutil.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define REACTOR_MAX_REQ 8192

/* dispatche request from c->in: parse reqln */
static void reactor_dispatch(int epfd, conn_t *c, const char *method, size_t method_len, const char *path_in, size_t path_len, int minor_version, const struct phr_header *headers, size_t num_headers, size_t header_bytes) {
  char *path, *qmark, *query;
  char inm_buf[80];
  char origin_buf[128];
  const char *if_none_match;
  const char *origin_hdr;
  route_t rt;
  pid_filter_t filter;
  unsigned pmt_pid;
  unsigned list_num;
  route_item_bufs_t item_bufs;
  client_info_t cinfo;
  int is_head, is_post, is_subscribe, is_unsubscribe, keep_alive;
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
  is_head = method_len == 4 && !memcmp(method, "HEAD", 4);
  is_post = method_len == 4 && !memcmp(method, "POST", 4);
  is_subscribe = method_len == 9 && !memcmp(method, "SUBSCRIBE", 9);
  is_unsubscribe = method_len == 11 && !memcmp(method, "UNSUBSCRIBE", 11);
  if (!is_head && !is_post && !is_subscribe && !is_unsubscribe && !(method_len == 3 && !memcmp(method, "GET", 3))) {
    respond_status(c, RESP_405, keep_alive);
    goto finish;
  }
  qmark = strchr(path, '?');
  query = NULL;
  if (qmark) {
    *qmark = '\0';
    query = qmark + 1;
  }

  if (is_post || is_subscribe || is_unsubscribe) {
    if (!reactor_cfg()->enable_dlna) {
      respond_status(c, RESP_404, keep_alive);
      goto finish;
    }
    if (is_post && (!strcmp(path, "/dlna/cd_control") || !strcmp(path, "/dlna/cm_control"))) {
      char clbuf[16];
      size_t body_len = 0;
      if (find_header(headers, num_headers, "Content-Length", clbuf, sizeof clbuf)) {
        char *end;
        unsigned long cl = strtoul(clbuf, &end, 10);
        if (*end == '\0')
          body_len = (size_t)cl;
      }
      serve_dlna_control(c, !strcmp(path, "/dlna/cd_control") ? "cd" : "cm", headers, num_headers,(char *)c->in.buf + c->req_bytes, body_len, keep_alive);
      c->req_bytes += body_len;
      goto finish;
    }
    if (is_subscribe && (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event"))) {
      serve_dlna_subscribe(c, !strcmp(path, "/dlna/cd_event") ? "cd" : "cm", headers, num_headers, keep_alive);
      goto finish;
    }
    if (is_unsubscribe && (!strcmp(path, "/dlna/cd_event") || !strcmp(path, "/dlna/cm_event"))) {
      serve_dlna_unsubscribe(c, headers, num_headers, keep_alive);
      goto finish;
    }
    respond_status(c, RESP_405, keep_alive);
    goto finish;
  }

  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters)
    pid_filter_parse_query(query, &filter);
  pmt_pid = pmt_select_parse_query(query);
  if (reactor_cfg()->http_auth[0] && (!strcmp(path, "/") || !strcmp(path, "/index.html") || !strcmp(path, "/ui/status.js") || !strcmp(path, "/ui/ws/") || !strcmp(path, "/ui/ws"))) {
    char authz_buf[200];
    const char *authz = find_header(headers, num_headers, "Authorization", authz_buf, sizeof authz_buf) ? authz_buf : NULL;
    if (!http_auth_ok(reactor_cfg(), authz)) {
      respond_401(c, keep_alive);
      goto finish;
    }
  }

  if (reactor_cfg()->enable_dlna && !strcmp(path, "/dlna/desc.xml")) {
    serve_dlna_desc(c, is_head, keep_alive);
    goto finish;
  }
  if (reactor_cfg()->enable_dlna && !strcmp(path, "/dlna/cd_scpd.xml")) {
    serve_dlna_cd_scpd(c, is_head, keep_alive);
    goto finish;
  }
  if (reactor_cfg()->enable_dlna && !strcmp(path, "/dlna/cm_scpd.xml")) {
    serve_dlna_cm_scpd(c, is_head, keep_alive);
    goto finish;
  }

  if (reactor_cfg()->metrics_http && !strcmp(path, "/metrics")) {
    serve_metrics(c, is_head, keep_alive);
    goto finish;
  }
  if (!strcmp(path, "/") || !strcmp(path, "/index.html")) {
    if (reactor_cfg()->no_status)
      respond_status(c, RESP_404, keep_alive);
    else
      serve_htdocs_index(c, is_head, keep_alive);
    goto finish;
  }
  if (!reactor_cfg()->no_status && !strcmp(path, "/ui/status.js")) {
    serve_status(c, is_head, keep_alive);
    goto finish;
  }
  if (!reactor_cfg()->no_status && !is_head && ws_try_upgrade(c, path, headers, num_headers, keep_alive))
    goto finish;

  if (!strncmp(path, "/export/", 8)) {
    route_fmt_t exp_fmt;
    playlist_type_t exp_ptype;
    char host_buf[128];
    const char *host_hdr = find_header(headers, num_headers, "Host", host_buf, sizeof host_buf) ? host_buf : NULL;
    if (playlist_path_parse(path, &exp_fmt, &exp_ptype) || playlist_fmt_disabled(reactor_cfg(), exp_fmt)) {
      respond_status(c, RESP_404, keep_alive);
      goto finish;
    }
    serve_playlist(c, exp_fmt, exp_ptype, host_hdr, query, &filter, is_head, keep_alive);
    goto finish;
  }

  if (route_parse(path, &rt)) {
    respond_status(c, RESP_404, keep_alive);
    goto finish;
  }
  if (route_disabled(&rt)) {
    respond_status(c, RESP_404, keep_alive);
    goto finish;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS:
    case ROUTE_FMT_RAWAUDIO: {
      /* unbounded body, must frame with connection close, no keep-alive */
      static const char ts_header[] = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n";
      static const char ra_header[] = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";
      const char *header = rt.fmt == ROUTE_FMT_RAWAUDIO ? ra_header : ts_header;
      unsigned tp_pmt_pid = rt.fmt == ROUTE_FMT_TS ? 0 : pmt_pid;
      int spts = rt.fmt == ROUTE_FMT_SPTS;
      int rawaudio = rt.fmt == ROUTE_FMT_RAWAUDIO;
      capture_ctx_t *ctx = open_source(&rt, &list_num);
      if (!ctx) {
        respond_status(c, RESP_404, keep_alive);
        break;
      }
      if (is_head) {
        capture_close(ctx);
        conn_queue(c, header, strlen(header));
        break;
      }
      {
        int sub;
        route_client_info(&rt, list_num, &filter, tp_pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
        sub = ts_push_subscribe(ctx, &filter, 1, c->fd, tp_pmt_pid, spts, rawaudio, &cinfo);
        if (sub < 0) {
          capture_close(ctx);
          respond_status(c, RESP_501, keep_alive);
          break;
        }
        c->slot = sub;
      }
      conn_queue(c, header, strlen(header));
      c->become_tspush = 1;
      break;
    }

    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4: {
      seg_container_t container = rt.fmt == ROUTE_FMT_HLS_FMP4 ? SEG_CONTAINER_FMP4 : SEG_CONTAINER_TS;
      int wsh;
      size_t bytes = 0;
      capture_ctx_t *ctx = open_source(&rt, &list_num);
      if (!ctx) {
        respond_status(c, RESP_404, keep_alive);
        break;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, container,
                          container == SEG_CONTAINER_FMP4 ? 0.0 : reactor_cfg()->hls_part_size)) {
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (!strcmp(rt.hls_file, "index.m3u8") && !hls_store_ready(ctx, &filter, pmt_pid, container) &&
          hls_cold_try_park(c, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_HLS, container, 0, is_head, keep_alive, origin_hdr,
                            (int)(reactor_cfg()->segment_size * 2000.0), wsh)) {
        c->state = CONN_DISPATCH;
        return;
      }
      if (hls_serve(c, ctx, &filter, pmt_pid, container, rt.hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes))
        ws_clients_add_bytes(wsh, bytes);
      else
        respond_status(c, RESP_404, keep_alive);
      break;
    }

    case ROUTE_FMT_LLHLS: {
      uint32_t want_seg;
      int want_part;
      int wsh;
      size_t bytes = 0;
      capture_ctx_t *ctx = open_source(&rt, &list_num);
      if (!ctx) {
        respond_status(c, RESP_404, keep_alive);
        break;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_TS, reactor_cfg()->hls_part_size)) {
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (!strcmp(rt.hls_file, "index_ll.m3u8") && !hls_ll_store_ready(ctx, &filter, pmt_pid, SEG_CONTAINER_TS) && hls_cold_try_park(c, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_LLHLS, SEG_CONTAINER_TS, 0, is_head, keep_alive, origin_hdr,
                            (int)(reactor_cfg()->segment_size * 2000.0), wsh)) {
        c->state = CONN_DISPATCH;
        return;
      }
      if (!strcmp(rt.hls_file, "index_ll.m3u8") && parse_blocking_reload(query, &want_seg, &want_part) &&
          !hls_part_available(ctx, &filter, pmt_pid, SEG_CONTAINER_TS, want_seg, want_part) &&
          llhls_try_park(c, ctx, &filter, pmt_pid, rt.hls_file, is_head, keep_alive, origin_hdr, want_seg, want_part, (int)(reactor_cfg()->hls_part_size * 2000.0), wsh)) {
        c->state = CONN_DISPATCH;
        return;
      }
      if (hls_serve_ll(c, ctx, &filter, pmt_pid, rt.hls_file, is_head, keep_alive, if_none_match, origin_hdr, &bytes)) {
        ws_clients_add_bytes(wsh, bytes);
      } else {
        respond_status(c, RESP_404, keep_alive);
      }
      break;
    }

    case ROUTE_FMT_DASH:
    case ROUTE_FMT_LLDASH: {
      int want_ll = rt.fmt == ROUTE_FMT_LLDASH;
      int wsh;
      size_t bytes = 0;
      capture_ctx_t *ctx = open_source(&rt, &list_num);
      if (!ctx) {
        respond_status(c, RESP_404, keep_alive);
        break;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 1, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, SEG_CONTAINER_FMP4,
                          want_ll ? reactor_cfg()->dash_part_size : 0.0)) {
        respond_status(c, RESP_501, keep_alive);
        break;
      }
      if (strcmp(rt.hls_file, "manifest.mpd") != 0) {
        if (!reactor_cfg()->no_lldash && !is_head && dash_lldash_try_attach(c, ctx, &filter, pmt_pid, rt.hls_file, keep_alive, origin_hdr, wsh))
          break;
        if (dash_serve_seg(c, ctx, &filter, pmt_pid, rt.hls_file, is_head, keep_alive, origin_hdr, &bytes))
          ws_clients_add_bytes(wsh, bytes);
        else
          respond_status(c, RESP_404, keep_alive);
        break;
      }
      if (!hls_store_ready(ctx, &filter, pmt_pid, SEG_CONTAINER_FMP4) &&
          hls_cold_try_park(c, ctx, &filter, pmt_pid, rt.hls_file, HLS_COLD_DASH, SEG_CONTAINER_FMP4, want_ll, is_head, keep_alive, origin_hdr, (int)(reactor_cfg()->segment_size * 2000.0), wsh)) {
        c->state = CONN_DISPATCH;
        return;
      }
      if (dash_serve(c, ctx, &filter, pmt_pid, want_ll, reactor_cfg()->dash_utc_url, is_head, keep_alive, origin_hdr, &bytes))
        ws_clients_add_bytes(wsh, bytes);
      else
        respond_status(c, RESP_404, keep_alive);
      break;
    }

    default:
      respond_status(c, RESP_501, keep_alive);
      break;
  }

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
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
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
  {
    char clbuf[16];
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
  }
  reactor_dispatch(epfd, c, method, method_len, path, path_len, minor_version, headers, num_headers, (size_t)pret);
}

void reactor_keepalive(int epfd, conn_t *c) {
  size_t leftover = c->in.len > c->req_bytes ? c->in.len - c->req_bytes : 0;

  if (leftover)
    memmove(c->in.buf, c->in.buf + c->req_bytes, leftover);
  c->in.off = 0;
  c->in.len = leftover;
  c->out.off = 0;
  c->out.len = 0;
  c->keep_alive = 0;
  c->state = CONN_READING;
  reactor_arm(epfd, c, 0);
  if (leftover) /* pipelined buffered B, dispatch without waiting for other sock reads */
    reactor_read(epfd, c);
}
