/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "../reactor/internal.h"
#include "../ts/ts_push.h"
#include "http2_int.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

/* DATA read callback: pulls straight from subscriber's h2_ring
   (SPSC, producer same reactor thread via h2_tspush_wake -> resume_data) */
static ssize_t tspush_read_cb(nghttp2_session *ng, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *ud) {
  (void)ng;
  (void)stream_id;
  (void)ud;
  h2_tspush_stream_t *tcs = source->ptr;
  ts_sub_t *sub = &g_ts_subs[tcs->sub_idx];
  uint32_t wpos = atomic_load_explicit(&sub->h2_wpos, memory_order_acquire);
  uint32_t rpos = atomic_load_explicit(&sub->h2_rpos, memory_order_relaxed);
  uint32_t idx, n;
  if (wpos == rpos) {
    *data_flags = NGHTTP2_DATA_FLAG_NO_END_STREAM;
    return NGHTTP2_ERR_DEFERRED;
  }
  idx = rpos & (TS_RING_H2_BYTES - 1u);
  n = wpos - rpos;
  if (n > TS_RING_H2_BYTES - idx)
    n = TS_RING_H2_BYTES - idx;
  if (n > length)
    n = (uint32_t)length;
  memcpy(buf, sub->h2_ring + idx, n);
  atomic_store_explicit(&sub->h2_rpos, rpos + n, memory_order_release);
  return (ssize_t)n;
}

static void h2_submit_tspush_response(h2_conn_t *conn, int32_t stream_id, h2_tspush_stream_t *tcs) {
  nghttp2_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)"content-type", (uint8_t *)"video/mp2t", 12, 10, NGHTTP2_NV_FLAG_NONE},
  };
  nghttp2_data_provider dp;
  dp.read_callback = tspush_read_cb;
  dp.source.ptr = tcs;
  nghttp2_submit_response(conn->ng, stream_id, nva, 2, &dp);
}

/* new ring data for sub_idx: resume DATA provider, arm EPOLLOUT for next
   h2_flush_tx */
void h2_tspush_wake(int sub_idx) {
  if (sub_idx < 0 || sub_idx >= g_ts_subs_n)
    return;
  int fd = g_ts_subs[sub_idx].fd;
  conn_t *c = conn_for_fd(fd);
  if (!c)
    return;
  h2_conn_t *conn = (h2_conn_t *)c->h2;
  if (!conn)
    return;
  h2_tspush_stream_t *tcs = NULL;
  for (int i = 0; i < H2_TSPUSH_MAX; i++)
    if (conn->tspush[i].sid && conn->tspush[i].sub_idx == sub_idx) {
      tcs = &conn->tspush[i];
      break;
    }
  if (!tcs)
    return;
  nghttp2_session_resume_data(conn->ng, tcs->sid);
  pthread_mutex_lock(&c->out_lock);
  if (!atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed))
    conn_epoll_mod(c, c->epfd, 1);
  pthread_mutex_unlock(&c->out_lock);
}

/* registers a TS push stream. 1 = dispatched, 0 = slot table full (caller sends an error response) */
int h2_tspush_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id, int tspush_sub) {
  int ci = -1;
  for (int i = 0; i < H2_TSPUSH_MAX; i++)
    if (!conn->tspush[i].sid) {
      ci = i;
      break;
    }
  if (ci < 0)
    return 0;
  h2_tspush_stream_t *tcs = &conn->tspush[ci];
  tcs->sub_idx = tspush_sub;
  tcs->sid = stream_id;
  h2_submit_tspush_response(conn, stream_id, tcs);
  g_ts_subs[tspush_sub].fd = c->fd;
  g_ts_subs[tspush_sub].h2_sid = stream_id;
  ts_push_set_reactor_tid(tspush_sub, t_reactor_tid);
  atomic_store_explicit(&g_ts_subs[tspush_sub].ready, 1, memory_order_release);
  return 1;
}

void h2_tspush_on_stream_close(h2_conn_t *conn, int32_t stream_id) {
  for (int i = 0; i < H2_TSPUSH_MAX; i++) {
    h2_tspush_stream_t *tcs = &conn->tspush[i];
    if (tcs->sid != stream_id)
      continue;
    int sub = tcs->sub_idx;
    tcs->sid = 0;
    tcs->sub_idx = -1;
    ts_push_unsubscribe_by_idx(sub);
    return;
  }
}

#endif /* HAVE_HTTP2 */
