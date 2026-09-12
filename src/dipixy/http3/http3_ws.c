/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#ifdef HAVE_HTTP3

#include "../reactor/internal.h"
#include "../reactor/reactor_tls.h"
#include "../reactor/ws_dispatch.h"
#include "../ws/ws_broadcast.h"
#include "../ws/ws_frame.h"
#include "../ws/ws_sources.h"
#include "../httpng/httpng.h"
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
  if (r->ws_send_off >= r->ws_send_len) return NGHTTP3_ERR_WOULDBLOCK;
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
  size_t hdr = ws_frame_hdr_len(len);
  size_t flen = hdr + len;
  pthread_mutex_lock(&r->ws_lock);
  if (!r->ws_active) {
    pthread_mutex_unlock(&r->ws_lock);
    return;
  }
  if (growbuf_reserve((void **)&r->ws_pending, &r->ws_pending_cap, 1, r->ws_pending_len + flen, 4096)) {
    pthread_mutex_unlock(&r->ws_lock);
    return;
  }
  ws_frame_encode(r->ws_pending + r->ws_pending_len, opcode, payload, len);
  r->ws_pending_len += flen;
  pthread_mutex_unlock(&r->ws_lock);
}

static void h3_ws_sink(void *ctx, const uint8_t *frame, size_t flen) { h3_ws_queue_prebuilt(ctx, frame, flen); }

static void h3_ws_queue_cb(void *ctx, int opcode, const void *payload, size_t len) { h3_ws_queue(ctx, opcode, payload, len); }

void h3_ws_data_chunk(h3_req_t *r, const uint8_t *data, size_t len) {
  int opcode, got;
  const uint8_t *payload;
  size_t plen;
  if (!r->ws_active || ws_parser_feed(&r->ws_parser, data, len)) return;
  for (;;) {
    got = ws_parser_next(&r->ws_parser, &opcode, &payload, &plen);
    if (got <= 0) return;
    ws_dispatch_frame(r, h3_ws_queue_cb, opcode, payload, plen);
  }
}

void h3_ws_dispatch(h3_conn_t *c, h3_req_t *r) {
  nghttp3_nv nva[] = {{(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE}};
  nghttp3_data_reader dr;
  ws_parser_init(&r->ws_parser);
  r->ws_pending = malloc(4096);
  r->ws_pending_cap = r->ws_pending ? 4096 : 0;
  r->ws_active = 1;
  c->ws_active_count++;
  dr.read_data = h3_ws_read_cb;
  nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
  nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, 1, &dr);
  ws_broadcast_register(h3_ws_sink, r);
}

static void h3_ws_req_cleanup(h3_conn_t *c, h3_req_t *r) {
  if (!r->ws_active) return;
  ws_broadcast_unregister(h3_ws_sink, r);
  pthread_mutex_lock(&r->ws_lock);
  r->ws_active = 0;
  pthread_mutex_unlock(&r->ws_lock);
  c->ws_active_count--;
  ws_parser_free(&r->ws_parser);
}

void h3_ws_on_stream_close(h3_conn_t *c, int64_t stream_id) {
  h3_req_t *r = find_req(c, stream_id);
  if (r) h3_ws_req_cleanup(c, r);
}

void h3_ws_on_conn_close(h3_conn_t *c) {
  for (int i = 0; i < H3_MAX_REQS; i++) if (c->reqs[i].active) h3_ws_req_cleanup(c, &c->reqs[i]);
}

/* per-reactor eventfd handler: resumes H3 WS streams with data queued from any thread */
void h3_ws_flush(void) {
  int ci, ri, fd;
  for (ci = 0; ci < t_h3_active_cnt; ci++) {
    h3_conn_t *c = t_h3_active[ci];
    if (c->done || !c->ws_active_count) continue;
    fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
    if (fd < 0) continue;
    for (ri = 0; ri < H3_MAX_REQS; ri++) {
      h3_req_t *r = &c->reqs[ri];
      uint8_t *data;
      size_t len;
      if (!r->active || !r->ws_active || r->ws_inflight) continue;
      if (!httpng_ws_drain_pending(&r->ws_lock, &r->ws_pending, &r->ws_pending_len, &r->ws_pending_cap, &data, &len)) continue;
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
