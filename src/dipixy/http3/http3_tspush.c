/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"

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
  uint32_t wpos = atomic_load_explicit(&sub->h3_wpos, memory_order_acquire);
  uint32_t rpos = atomic_load_explicit(&sub->h3_rpos, memory_order_relaxed);
  uint32_t idx, avail, contig;
  if (wpos == rpos)
    return NGHTTP3_ERR_WOULDBLOCK;
  idx = rpos & (TS_RING_H3_BYTES - 1u);
  avail = wpos - rpos;
  contig = TS_RING_H3_BYTES - idx;
  if (contig > avail)
    contig = avail;
  vec[0].base = sub->h3_ring + idx;
  vec[0].len = contig;
  *pflags = NGHTTP3_DATA_FLAG_NONE;
  atomic_store_explicit(&sub->h3_rpos, rpos + contig, memory_order_release);
  return 1;
}

/* per-reactor tspush eventfd handler: resumes H3 streams with ring data */
void ts_push_h3_flush(void) {
  for (int ci = 0; ci < t_h3_active_cnt; ci++) {
    h3_conn_t *c = t_h3_active[ci];
    int fd;
    if (c->done) continue;
    fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
    if (fd < 0) continue;
    for (int ri = 0; ri < H3_MAX_REQS; ri++) {
      h3_req_t *r = &c->reqs[ri];
      ts_sub_t *sub;
      uint32_t wpos, rpos;
      if (!r->active || r->tspush_sub_idx < 0) continue;
      sub = &g_ts_subs[r->tspush_sub_idx];
      wpos = atomic_load_explicit(&sub->h3_wpos, memory_order_acquire);
      rpos = atomic_load_explicit(&sub->h3_rpos, memory_order_relaxed);
      if (wpos == rpos) continue;
      nghttp3_conn_resume_stream(c->h3conn, r->stream_id);
      flush_tx(c, fd);
    }
  }
}

#endif /* HAVE_HTTP3 */
