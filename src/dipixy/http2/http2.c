/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "http2.h"
#include "http2_int.h"

#include "../ts/capture/capture.h"
#include "../ts/channels/channels.h"
#include "../hls/hls.h"
#include "../hls/segment/segment.h"
#include "../ts/pidfilter.h"
#include "../ts/pmtselect.h"
#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../core/route.h"
#include "../ts/ts_push.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#define H2_READ_BUF 65536

static h2_stream_t *h2_find_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++)
    if (conn->streams[i].id == id)
      return &conn->streams[i];
  return NULL;
}

static h2_stream_t *h2_alloc_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++) {
    if (!conn->streams[i].id) {
      memset(&conn->streams[i], 0, sizeof(h2_stream_t));
      conn->streams[i].id = id;
      return &conn->streams[i];
    }
  }
  return NULL;
}

static void h2_free_stream(h2_conn_t *conn, int32_t id) {
  for (int i = 0; i < H2_MAX_STREAMS; i++)
    if (conn->streams[i].id == id)
      conn->streams[i].id = 0;
}

static int cb_begin_headers(nghttp2_session *ng, const nghttp2_frame *frame, void *ud) {
  h2_conn_t *conn = ud;
  if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) return 0;
  if (!h2_alloc_stream(conn, frame->hd.stream_id)) nghttp2_submit_rst_stream(ng, NGHTTP2_FLAG_NONE, frame->hd.stream_id, NGHTTP2_REFUSED_STREAM);
  return 0;
}

static int cb_on_header(nghttp2_session *ng, const nghttp2_frame *frame, const uint8_t *name, size_t namelen, const uint8_t *value, size_t valuelen, uint8_t flags, void *ud) {
  (void)ng;
  (void)flags;
  h2_conn_t *conn = ud;
  h2_stream_t *s = h2_find_stream(conn, frame->hd.stream_id);
  if (!s) return 0;
  if (namelen == 7 && memcmp(name, ":method", 7) == 0) {
    size_t n = valuelen < sizeof(s->method) - 1 ? valuelen : sizeof(s->method) - 1;
    memcpy(s->method, value, n);
    s->method[n] = '\0';
  } else if (namelen == 5 && memcmp(name, ":path", 5) == 0) {
    size_t n = valuelen < sizeof(s->path) - 1 ? valuelen : sizeof(s->path) - 1;
    memcpy(s->path, value, n);
    s->path[n] = '\0';
  } else if (namelen == 13 && memcmp(name, "if-none-match", 13) == 0) {
    size_t n = valuelen < sizeof(s->inm) - 1 ? valuelen : sizeof(s->inm) - 1;
    memcpy(s->inm, value, n);
    s->inm[n] = '\0';
    strip_etag_quotes(s->inm);
  } else if (namelen == 6 && memcmp(name, "origin", 6) == 0) {
    size_t n = valuelen < sizeof(s->origin) - 1 ? valuelen : sizeof(s->origin) - 1;
    memcpy(s->origin, value, n);
    s->origin[n] = '\0';
  } else if (namelen == 13 && memcmp(name, "authorization", 13) == 0) {
    size_t n = valuelen < sizeof(s->authz) - 1 ? valuelen : sizeof(s->authz) - 1;
    memcpy(s->authz, value, n);
    s->authz[n] = '\0';
  } else if (namelen == 9 && memcmp(name, ":protocol", 9) == 0) {
    size_t n = valuelen < sizeof(s->protocol) - 1 ? valuelen : sizeof(s->protocol) - 1;
    memcpy(s->protocol, value, n);
    s->protocol[n] = '\0';
  }
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
    h2_stream_t *s = h2_find_stream(conn, frame->hd.stream_id);
    if (s) s->dispatch_pending = 1;
  }
  if (frame->hd.type == NGHTTP2_GOAWAY)
    conn->done = 1;
  return 0;
}

static int cb_stream_close(nghttp2_session *ng, int32_t stream_id, uint32_t error_code, void *ud) {
  (void)ng;
  (void)error_code;
  h2_conn_t *conn = ud;
  h2_free_stream(conn, stream_id);
  h2_tspush_on_stream_close(conn, stream_id);
  h2_llhls_on_stream_close(conn, stream_id);
  h2_ws_on_stream_close(conn, stream_id);
  return 0;
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

/* matches reactor/dispatch.c's HTTP/1.1 dispatch */
static void h2_dispatch_stream(h2_conn_t *conn, conn_t *c, h2_stream_t *stream) {
  route_t rt;
  pid_filter_t filter;
  unsigned pmt_pid;
  unsigned list_num;
  route_item_bufs_t item_bufs;
  client_info_t cinfo;
  char *qmark, *query;
  int is_head;
  capture_ctx_t *ctx;

  if (!strcmp(stream->method, "CONNECT") && !strcmp(stream->protocol, "websocket")) {
    if (reactor_cfg()->no_status || strcmp(stream->path, "/ui/ws/"))
      h2_respond_status(conn, stream->id, "404");
    else if (!http_auth_ok(reactor_cfg(), stream->authz[0] ? stream->authz : NULL))
      h2_respond_401(conn, stream->id);
    else
      h2_ws_dispatch(conn, c, stream->id);
    return;
  }

  if (strcmp(stream->method, "GET") != 0 && strcmp(stream->method, "HEAD") != 0) {
    h2_respond_status(conn, stream->id, "405");
    return;
  }
  is_head = !strcmp(stream->method, "HEAD");
  qmark = strchr(stream->path, '?');
  query = qmark ? qmark + 1 : NULL;
  filter.count = 0;
  if (!reactor_cfg()->no_pid_filters)
    pid_filter_parse_query(query, &filter);
  pmt_pid = pmt_select_parse_query(query);
  if (qmark) *qmark = '\0';
  if (route_parse(stream->path, &rt) || route_disabled(&rt)) {
    h2_respond_status(conn, stream->id, "404");
    return;
  }

  switch (rt.fmt) {
    case ROUTE_FMT_TS:
    case ROUTE_FMT_SPTS: {
      int sub;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h2_respond_status(conn, stream->id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, rt.fmt == ROUTE_FMT_SPTS ? pmt_pid : 0, c->client_ip, 2, &item_bufs, &cinfo);
      sub = ts_push_subscribe(ctx, &filter, 2, c->fd, rt.fmt == ROUTE_FMT_SPTS ? pmt_pid : 0, rt.fmt == ROUTE_FMT_SPTS, 0, &cinfo);
      if (sub < 0) {
        capture_close(ctx);
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (!h2_tspush_dispatch(conn, c, stream->id, sub)) {
        ts_push_unsubscribe_by_idx(sub);
        h2_respond_status(conn, stream->id, "501");
      }
      return;
    }

    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4: {
      hls_container_t container = rt.fmt == ROUTE_FMT_HLS_FMP4 ? HLS_CONTAINER_FMP4 : HLS_CONTAINER_TS;
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h2_respond_status(conn, stream->id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 2, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, container, reactor_cfg()->hls_part_size)) {
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      handled = hls_render(ctx, &filter, pmt_pid, container, rt.hls_file, is_head, stream->inm[0] ? stream->inm : NULL, &resp);
      h2_respond_hls(conn, stream->id, handled, &resp, stream->origin[0] ? stream->origin : NULL);
      if (handled && resp.status == 200) ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    case ROUTE_FMT_LLHLS: {
      uint32_t want_seg;
      int want_part;
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h2_respond_status(conn, stream->id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 2, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, HLS_CONTAINER_TS, reactor_cfg()->hls_part_size)) {
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (!strcmp(rt.hls_file, "index_ll.m3u8") && parse_blocking_reload(query, &want_seg, &want_part) &&
          !hls_part_available(ctx, &filter, pmt_pid, want_seg, want_part) &&
          h2_llhls_try_park(conn, c, stream->id, ctx, &filter, pmt_pid, rt.hls_file, is_head,
                             stream->inm[0] ? stream->inm : NULL, stream->origin[0] ? stream->origin : NULL, want_seg, want_part,
                             (int)(reactor_cfg()->hls_part_size * 2000.0), wsh))
        return;
      handled = hls_render_ll(ctx, &filter, pmt_pid, rt.hls_file, is_head, stream->inm[0] ? stream->inm : NULL, &resp);
      h2_respond_hls(conn, stream->id, handled, &resp, stream->origin[0] ? stream->origin : NULL);
      if (handled && resp.status == 200)
        ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    case ROUTE_FMT_DASH: {
      hls_resp_t resp;
      int wsh, handled;
      ctx = open_source(&rt, &list_num);
      if (!ctx) {
        h2_respond_status(conn, stream->id, "404");
        return;
      }
      route_client_info(&rt, list_num, &filter, pmt_pid, c->client_ip, 2, &item_bufs, &cinfo);
      wsh = ws_clients_touch(&cinfo);
      if (wsh < 0) {
        capture_close(ctx);
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (!hls_seg_touch(ctx, &filter, pmt_pid, reactor_cfg()->segment_size, reactor_cfg()->segment_count, HLS_CONTAINER_FMP4, reactor_cfg()->hls_part_size)) {
        h2_respond_status(conn, stream->id, "501");
        return;
      }
      if (strcmp(rt.hls_file, "manifest.mpd") != 0)
        handled = hls_render_dash_seg(ctx, &filter, pmt_pid, rt.hls_file, is_head, &resp);
      else
        handled = hls_render_dash(ctx, &filter, pmt_pid, is_head, &resp);
      h2_respond_hls(conn, stream->id, handled, &resp, stream->origin[0] ? stream->origin : NULL);
      if (handled && resp.status == 200)
        ws_clients_add_bytes(wsh, resp.body_len);
      return;
    }

    default:
      h2_respond_status(conn, stream->id, "501");
      return;
  }
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
  conn_publish(c);      /* h2_tspush_wake() finds conns by fd */
  h2_flush_tx(conn, c); /* flush initial SETTINGS, might arm EPOLLOUT */
  return 0;
}

void h2_handle_readable(int epfd, conn_t *c) {
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  uint8_t inbuf[H2_READ_BUF];
  for (;;) {
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

  for (int i = 0; i < H2_MAX_STREAMS; i++) {
    if (conn->streams[i].id && conn->streams[i].dispatch_pending) {
      int32_t sid = conn->streams[i].id;
      conn->streams[i].dispatch_pending = 0;
      h2_dispatch_stream(conn, c, &conn->streams[i]);
      h2_free_stream(conn, sid);
    }
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
  if (conn->done ||
      (!nghttp2_session_want_read(conn->ng) && !nghttp2_session_want_write(conn->ng)))
    h2_conn_close(epfd, c);
}

void h2_conn_close(int epfd, conn_t *c) {
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  for (int i = 0; i < H2_TSPUSH_MAX; i++) if (conn->tspush[i].sid) h2_tspush_on_stream_close(conn, conn->tspush[i].sid);
  h2_llhls_on_conn_close(conn);
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
