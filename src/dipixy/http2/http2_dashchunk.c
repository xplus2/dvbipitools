/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "../reactor/internal.h"
#include "../dash/lldash.h"
#include "http2_int.h"

#include <string.h>

/* DATA read callback: pulls from the subscriber's h2 ring (dash/lldash.c), EOF once finalized and drained, RST if the ring ever overflowed */
static ssize_t dashchunk_read_cb(nghttp2_session *ng, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *ud) {
  (void)ng;
  (void)stream_id;
  (void)ud;
  h2_dashchunk_stream_t *tcs = source->ptr;
  size_t n;
  if (dash_lldash_ring_errored(tcs->sub_idx)) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  n = dash_lldash_ring_read(tcs->sub_idx, buf, length);
  if (n == 0) {
    if (dash_lldash_sub_finalized(tcs->sub_idx)) {
      *data_flags |= NGHTTP2_DATA_FLAG_EOF;
      return 0;
    }
    *data_flags = NGHTTP2_DATA_FLAG_NO_END_STREAM;
    return NGHTTP2_ERR_DEFERRED;
  }
  return (ssize_t)n;
}

static void h2_submit_dashchunk_response(h2_conn_t *conn, int32_t stream_id, h2_dashchunk_stream_t *tcs) {
  nghttp2_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)"content-type", (uint8_t *)"video/mp4", 12, 9, NGHTTP2_NV_FLAG_NONE},
  };
  nghttp2_data_provider dp;
  dp.read_callback = dashchunk_read_cb;
  dp.source.ptr = tcs;
  nghttp2_submit_response(conn->ng, stream_id, nva, 2, &dp);
}

void h2_dashchunk_wake(int sub_idx) {
  conn_t *c = dash_lldash_sub_h2c(sub_idx);
  h2_dashchunk_stream_t *tcs = dash_lldash_sub_h2_slot(sub_idx);
  h2_wake_stream(c, tcs ? tcs->sid : 0);
}

/* registers a dash-chunk stream. 1 = dispatched, 0 = slot table full (caller falls through) */
int h2_dashchunk_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id, int sub_idx, int ws_handle) {
  int ci = -1;
  for (int i = 0; i < H2_DASHCHUNK_MAX; i++) if (!conn->dashchunk[i].sid) {
    ci = i;
    break;
  }
  if (ci < 0) return 0;
  h2_dashchunk_stream_t *tcs = &conn->dashchunk[ci];
  tcs->sub_idx = sub_idx;
  tcs->sid = stream_id;
  h2_submit_dashchunk_response(conn, stream_id, tcs);
  dash_lldash_h2_bind(sub_idx, c, tcs, t_reactor_tid, ws_handle);
  return 1;
}

void h2_dashchunk_on_stream_close(h2_conn_t *conn, int32_t stream_id) {
  for (int i = 0; i < H2_DASHCHUNK_MAX; i++) {
    h2_dashchunk_stream_t *tcs = &conn->dashchunk[i];
    if (tcs->sid != stream_id) continue;
    int sub = tcs->sub_idx;
    tcs->sid = 0;
    tcs->sub_idx = -1;
    dash_lldash_sub_close(sub);
    return;
  }
}

#endif /* HAVE_HTTP2 */
