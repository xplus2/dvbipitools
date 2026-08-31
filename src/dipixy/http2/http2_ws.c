/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#define _GNU_SOURCE
#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../ws/ws_broadcast.h"
#include "../ws/ws_sources.h"
#include "http2_int.h"
#include "lib/helper/ioutil.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>

static ssize_t ws_read_cb(nghttp2_session *ng, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *ud) {
  (void)ng;
  (void)stream_id;
  (void)ud;
  h2_ws_stream_t *ws = source->ptr;
  if (!ws->send_len) {
    *data_flags = NGHTTP2_DATA_FLAG_NO_END_STREAM;
    return NGHTTP2_ERR_DEFERRED;
  }
  size_t n = ws->send_len - ws->send_off;
  if (n > length)
    n = length;
  memcpy(buf, ws->send_data + ws->send_off, n);
  ws->send_off += n;
  if (ws->send_off >= ws->send_len) {
    free(ws->send_data);
    ws->send_data = NULL;
    ws->send_len = 0;
    ws->send_off = 0;
    ws->inflight = 0;
  }
  return (ssize_t)n;
}

static void queue_prebuilt_frame(h2_ws_stream_t *ws, const uint8_t *frame, size_t flen) {
  conn_t *c = ws->c;
  pthread_mutex_lock(&ws->pending_lock);
  if (!ws->sid) {
    pthread_mutex_unlock(&ws->pending_lock);
    return;
  }
  if (growbuf_reserve((void **)&ws->pending, &ws->pending_cap, 1, ws->pending_len + flen, 4096)) {
    pthread_mutex_unlock(&ws->pending_lock);
    return;
  }
  memcpy(ws->pending + ws->pending_len, frame, flen);
  ws->pending_len += flen;
  pthread_mutex_unlock(&ws->pending_lock);
  if (!atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed))
    conn_epoll_mod(c, c->epfd, 1);
}

static void queue_frame(h2_ws_stream_t *ws, int opcode, const void *payload, size_t len) {
  size_t flen;
  uint8_t *frame = ws_build_frame(opcode, payload, len, &flen);
  if (!frame)
    return;
  queue_prebuilt_frame(ws, frame, flen);
  free(frame);
}

static void ws_sink(void *ctx, const uint8_t *frame, size_t flen) { queue_prebuilt_frame(ctx, frame, flen); }

void h2_ws_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id) {
  nghttp2_nv nva[] = {{(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP2_NV_FLAG_NONE}};
  nghttp2_data_provider dp;
  int i, slot = -1;

  for (i = 0; i < H2_WS_MAX; i++)
    if (!conn->ws[i].sid) {
      slot = i;
      break;
    }
  if (slot < 0) {
    nghttp2_nv err[] = {{(uint8_t *)":status", (uint8_t *)"503", 7, 3, NGHTTP2_NV_FLAG_NONE}};
    nghttp2_submit_response(conn->ng, stream_id, err, 1, NULL);
    return;
  }

  memset(&conn->ws[slot], 0, sizeof conn->ws[slot]);
  conn->ws[slot].sid = stream_id;
  conn->ws[slot].c = c;
  ws_parser_init(&conn->ws[slot].parser);
  conn->ws[slot].pending = malloc(4096);
  conn->ws[slot].pending_cap = conn->ws[slot].pending ? 4096 : 0;

  dp.read_callback = ws_read_cb;
  dp.source.ptr = &conn->ws[slot];
  nghttp2_submit_response(conn->ng, stream_id, nva, 1, &dp);
  ws_broadcast_register(ws_sink, &conn->ws[slot]);
}

static h2_ws_stream_t *find_ws(h2_conn_t *conn, int32_t stream_id) {
  int i;
  for (i = 0; i < H2_WS_MAX; i++)
    if (conn->ws[i].sid == stream_id)
      return &conn->ws[i];
  return NULL;
}

void h2_ws_data_chunk(h2_conn_t *conn, int32_t stream_id, const uint8_t *data, size_t len) {
  h2_ws_stream_t *ws = find_ws(conn, stream_id);
  int opcode, got;
  const uint8_t *payload;
  size_t plen;

  if (!ws)
    return;
  if (ws_parser_feed(&ws->parser, data, len))
    return;

  for (;;) {
    got = ws_parser_next(&ws->parser, &opcode, &payload, &plen);
    if (got <= 0)
      return;
    if (opcode == WS_OP_PING) {
      queue_frame(ws, WS_OP_PONG, payload, plen);
    } else if (opcode == WS_OP_CLOSE) {
      queue_frame(ws, WS_OP_CLOSE, payload, plen <= 125 ? plen : 0);
    } else if (opcode == WS_OP_TEXT) {
      char *json;
      if (memmem(payload, plen, "\"playlists.reload\"", 18)) {
        reactor_reload_channels();
        continue;
      }
      if (memmem(payload, plen, "\"tls.reload\"", 12)) {
        const char *resp = reload_tls() == 0 ? "{\"type\":\"tls.reload\",\"ok\":true}" : "{\"type\":\"tls.reload\",\"ok\":false}";
        queue_frame(ws, WS_OP_TEXT, resp, strlen(resp));
        continue;
      }
      if (memmem(payload, plen, "\"clients.get\"", 13)) {
        if (!ws_clients_build_snapshot(&json))
          queue_frame(ws, WS_OP_TEXT, json, strlen(json));
        continue;
      }
      if (!memmem(payload, plen, "\"sources.get\"", 13))
        continue;
      if (ws_sources_build_snapshot(reactor_cfg(), reactor_channels(), &json))
        continue;
      queue_frame(ws, WS_OP_TEXT, json, strlen(json));
    }
  }
}

void h2_ws_flush(h2_conn_t *conn) {
  int i;
  for (i = 0; i < H2_WS_MAX; i++) {
    h2_ws_stream_t *ws = &conn->ws[i];
    if (!ws->sid || ws->inflight)
      continue;
    pthread_mutex_lock(&ws->pending_lock);
    uint8_t *data = ws->pending;
    size_t len = ws->pending_len;
    ws->pending = NULL;
    ws->pending_len = 0;
    ws->pending_cap = 0;
    pthread_mutex_unlock(&ws->pending_lock);
    if (!len) {
      free(data);
      continue;
    }
    ws->send_data = data;
    ws->send_len = len;
    ws->send_off = 0;
    ws->inflight = 1;
    nghttp2_session_resume_data(conn->ng, ws->sid);
  }
}

static void ws_stream_cleanup(h2_ws_stream_t *ws) {
  ws_broadcast_unregister(ws_sink, ws);
  pthread_mutex_lock(&ws->pending_lock);
  free(ws->pending);
  free(ws->send_data);
  ws->sid = 0;
  pthread_mutex_unlock(&ws->pending_lock);
  ws_parser_free(&ws->parser);
}

void h2_ws_on_stream_close(h2_conn_t *conn, int32_t stream_id) {
  h2_ws_stream_t *ws = find_ws(conn, stream_id);
  if (ws)
    ws_stream_cleanup(ws);
}

void h2_ws_on_conn_close(h2_conn_t *conn) {
  int i;
  for (i = 0; i < H2_WS_MAX; i++) if (conn->ws[i].sid) ws_stream_cleanup(&conn->ws[i]);
}

#endif /* HAVE_HTTP2 */
