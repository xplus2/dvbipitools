/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"
#include "lib/helper/byte_ring.h"

/* data reader for TS push streams: drains per-subscriber SPSC ring */
nghttp3_ssize h3_tspush_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud) {
  (void)h3;
  (void)sid;
  (void)veccnt;
  (void)conn_ud;
  h3_req_t *r = stream_ud;
  if (!r || r->tspush_sub_idx < 0) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }
  ts_sub_t *sub = &g_ts_subs[r->tspush_sub_idx];
  if (atomic_load_explicit(&sub->alive, memory_order_acquire) != TS_SUB_ALIVE) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }
  size_t contig;
  const uint8_t *p = byte_ring_peek(&sub->h3_ring, &contig);
  if (!p) return NGHTTP3_ERR_WOULDBLOCK;
  vec[0].base = (uint8_t *)p;
  vec[0].len = contig;
  *pflags = NGHTTP3_DATA_FLAG_NONE;
  byte_ring_advance(&sub->h3_ring, contig);
  return 1;
}

/* registers TS push stream, submits response. always 1 = dispatched (h3 has
   no separate slot table, unlike h2) */
int h3_tspush_dispatch(h3_conn_t *c, h3_req_t *r, int sub) {
  nghttp3_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)"content-type", (uint8_t *)"video/mp2t", 12, 10, NGHTTP3_NV_FLAG_NONE},
  };
  nghttp3_data_reader dr;
  g_ts_subs[sub].h3c = c;
  g_ts_subs[sub].h3_sid = r->stream_id;
  ts_push_set_reactor_tid(sub, t_reactor_tid);
  r->tspush_sub_idx = sub;
  dr.read_data = h3_tspush_read_cb;
  nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
  nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, 2, &dr);
  atomic_store_explicit(&g_ts_subs[sub].ready, 1, memory_order_release);
  return 1;
}

void h3_tspush_wake(int sub_idx) {
  ts_sub_t *sub;
  h3_conn_t *c;
  int fd;
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n) return;
  sub = &g_ts_subs[sub_idx];
  c = sub->h3c;
  if (!c || c->done) return;
  fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd < 0) return;
  nghttp3_conn_resume_stream(c->h3conn, sub->h3_sid);
  flush_tx(c, fd);
}

#endif /* HAVE_HTTP3 */
