/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#ifdef HAVE_HTTP3

#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../ws/ws_broadcast.h"
#include "../ws/ws_frame.h"
#include "../ws/ws_sources.h"
#include "http3.h"
#include "http3_int.h"
#include "lib/helper/ioutil.h"

#include <stdlib.h>
#include <string.h>

static nghttp3_ssize h3_ws_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud) {
  h3_req_t *r = stream_ud;
  (void)h3;
  (void)sid;
  (void)veccnt;
  (void)conn_ud;
  if (!r || !r->ws_active) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }
  if (r->ws_send_off >= r->ws_send_len)
    return NGHTTP3_ERR_WOULDBLOCK;
  vec[0].base = r->ws_send_data + r->ws_send_off;
  vec[0].len = r->ws_send_len - r->ws_send_off;
  r->ws_send_off = r->ws_send_len;
  r->ws_inflight = 0; /* data itself stays alive: freed a cycle later via ws_prev_data */
  *pflags = NGHTTP3_DATA_FLAG_NONE;
  return 1;
}

static void h3_ws_queue_prebuilt(h3_req_t *r, const uint8_t *frame, size_t flen) {
  pthread_mutex_lock(&r->ws_lock);
  if (!r->ws_active) {
    pthread_mutex_unlock(&r->ws_lock);
    return;
  }
  if (growbuf_reserve((void **)&r->ws_pending, &r->ws_pending_cap, 1, r->ws_pending_len + flen, 4096)) {
    pthread_mutex_unlock(&r->ws_lock);
    return;
  }
  memcpy(r->ws_pending + r->ws_pending_len, frame, flen);
  r->ws_pending_len += flen;
  pthread_mutex_unlock(&r->ws_lock);
}

static void h3_ws_queue(h3_req_t *r, int opcode, const void *payload, size_t len) {
  size_t flen;
  uint8_t *frame = ws_build_frame(opcode, payload, len, &flen);
  if (!frame) return;
  h3_ws_queue_prebuilt(r, frame, flen);
  free(frame);
}

static void h3_ws_sink(void *ctx, const uint8_t *frame, size_t flen) { h3_ws_queue_prebuilt(ctx, frame, flen); }

void h3_ws_data_chunk(h3_req_t *r, const uint8_t *data, size_t len) {
  int opcode, got;
  const uint8_t *payload;
  size_t plen;

  if (!r->ws_active || ws_parser_feed(r->ws_parser, data, len)) return;
  for (;;) {
    got = ws_parser_next(r->ws_parser, &opcode, &payload, &plen);
    if (got <= 0) return;
    if (opcode == WS_OP_PING) {
      h3_ws_queue(r, WS_OP_PONG, payload, plen);
    } else if (opcode == WS_OP_CLOSE) {
      h3_ws_queue(r, WS_OP_CLOSE, payload, plen <= 125 ? plen : 0);
    } else if (opcode == WS_OP_TEXT) {
      char *json;
      if (memmem(payload, plen, "\"playlists.reload\"", 18)) {
        reactor_reload_channels();
        continue;
      }
      if (memmem(payload, plen, "\"tls.reload\"", 12)) {
        const char *resp = reload_tls() == 0 ? "{\"type\":\"tls.reload\",\"ok\":true}" : "{\"type\":\"tls.reload\",\"ok\":false}";
        h3_ws_queue(r, WS_OP_TEXT, resp, strlen(resp));
        continue;
      }
      if (memmem(payload, plen, "\"clients.get\"", 13)) {
        if (!ws_clients_build_snapshot(&json))
          h3_ws_queue(r, WS_OP_TEXT, json, strlen(json));
        continue;
      }
      if (!memmem(payload, plen, "\"sources.get\"", 13))
        continue;
      if (ws_sources_build_snapshot(reactor_cfg(), reactor_channels(), &json))
        continue;
      h3_ws_queue(r, WS_OP_TEXT, json, strlen(json));
    }
  }
}

#define H3_WS_PARSER_POOL_MAX H3_MAX_REQS

static _Thread_local ws_parser_t *t_h3_ws_parser_pool[H3_WS_PARSER_POOL_MAX];
static _Thread_local int t_h3_ws_parser_pool_n;

static ws_parser_t *h3_ws_parser_alloc(void) {
  if (t_h3_ws_parser_pool_n > 0) return t_h3_ws_parser_pool[--t_h3_ws_parser_pool_n];
  return malloc(sizeof(ws_parser_t));
}

static void h3_ws_parser_release(ws_parser_t *p) {
  if (t_h3_ws_parser_pool_n < H3_WS_PARSER_POOL_MAX)
    t_h3_ws_parser_pool[t_h3_ws_parser_pool_n++] = p;
  else
    free(p);
}

void h3_ws_dispatch(h3_conn_t *c, h3_req_t *r) {
  nghttp3_nv nva[] = {{(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE}};
  nghttp3_data_reader dr;

  r->ws_parser = h3_ws_parser_alloc();
  if (!r->ws_parser) {
    h3_respond_status(c, r->stream_id, "503");
    return;
  }
  ws_parser_init(r->ws_parser);
  r->ws_pending = malloc(4096);
  r->ws_pending_cap = r->ws_pending ? 4096 : 0;
  r->ws_active = 1;
  dr.read_data = h3_ws_read_cb;
  nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
  nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, 1, &dr);
  ws_broadcast_register(h3_ws_sink, r);
}

static void h3_ws_req_cleanup(h3_req_t *r) {
  if (!r->ws_active) return;
  ws_broadcast_unregister(h3_ws_sink, r);
  pthread_mutex_lock(&r->ws_lock);
  r->ws_active = 0;
  pthread_mutex_unlock(&r->ws_lock);
  ws_parser_free(r->ws_parser);
  h3_ws_parser_release(r->ws_parser);
  r->ws_parser = NULL;
}

void h3_ws_on_stream_close(h3_conn_t *c, int64_t stream_id) {
  h3_req_t *r = find_req(c, stream_id);
  if (r) h3_ws_req_cleanup(r);
}

void h3_ws_on_conn_close(h3_conn_t *c) {
  for (int i = 0; i < H3_MAX_REQS; i++) if (c->reqs[i].active) h3_ws_req_cleanup(&c->reqs[i]);
}

/* per-reactor eventfd handler: resumes H3 WS streams with data queued from any thread */
void h3_ws_flush(void) {
  int ci, ri, fd;
  for (ci = 0; ci < t_h3_active_cnt; ci++) {
    h3_conn_t *c = t_h3_active[ci];
    if (c->done) continue;
    fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
    if (fd < 0)
      continue;
    for (ri = 0; ri < H3_MAX_REQS; ri++) {
      h3_req_t *r = &c->reqs[ri];
      uint8_t *data;
      size_t len;
      if (!r->active || !r->ws_active || r->ws_inflight) continue;
      pthread_mutex_lock(&r->ws_lock);
      data = r->ws_pending;
      len = r->ws_pending_len;
      r->ws_pending = NULL;
      r->ws_pending_len = 0;
      r->ws_pending_cap = 0;
      pthread_mutex_unlock(&r->ws_lock);
      if (!len) {
        free(data);
        continue;
      }
      free(r->ws_prev_data);
      r->ws_prev_data = r->ws_send_data;
      r->ws_send_data = data;
      r->ws_send_len = len;
      r->ws_send_off = 0;
      r->ws_inflight = 1;
      nghttp3_conn_resume_stream(c->h3conn, r->stream_id);
      flush_tx(c, fd);
    }
  }
}
#endif /* HAVE_HTTP3 */
