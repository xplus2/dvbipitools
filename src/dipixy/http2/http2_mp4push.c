/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP2

#include "../reactor/internal.h"
#include "../segment/mp4push.h"
#include "http2_int.h"

#include <string.h>

static ssize_t mp4push_read_cb(nghttp2_session *ng, int32_t stream_id, uint8_t *buf, size_t length, uint32_t *data_flags, nghttp2_data_source *source, void *ud) {
  (void)ng;
  (void)stream_id;
  (void)ud;
  h2_mp4push_stream_t *tcs = source->ptr;
  size_t n;
  if (mp4push_ring_errored(tcs->sub_idx)) return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  n = mp4push_ring_read(tcs->sub_idx, buf, length);
  if (n == 0) {
    *data_flags = NGHTTP2_DATA_FLAG_NO_END_STREAM;
    return NGHTTP2_ERR_DEFERRED;
  }
  return (ssize_t)n;
}

static void h2_submit_mp4push_response(h2_conn_t *conn, int32_t stream_id, h2_mp4push_stream_t *tcs) {
  nghttp2_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"200", 7, 3, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)"content-type", (uint8_t *)"video/mp4", 12, 9, NGHTTP2_NV_FLAG_NONE},
  };
  nghttp2_data_provider dp;
  dp.read_callback = mp4push_read_cb;
  dp.source.ptr = tcs;
  nghttp2_submit_response(conn->ng, stream_id, nva, 2, &dp);
}

void h2_mp4push_wake(int sub_idx) {
  conn_t *c = mp4push_sub_h2c(sub_idx);
  h2_mp4push_stream_t *tcs = mp4push_sub_h2_slot(sub_idx);
  h2_wake_stream(c, tcs ? tcs->sid : 0);
}

int h2_mp4push_dispatch(h2_conn_t *conn, conn_t *c, int32_t stream_id, int sub_idx, int ws_handle) {
  int ci = -1;
  for (int i = 0; i < H2_MP4PUSH_MAX; i++) if (!conn->mp4push[i].sid) {
    ci = i;
    break;
  }
  if (ci < 0) return 0;
  h2_mp4push_stream_t *tcs = &conn->mp4push[ci];
  tcs->sub_idx = sub_idx;
  tcs->sid = stream_id;
  h2_submit_mp4push_response(conn, stream_id, tcs);
  mp4push_h2_bind(sub_idx, c, tcs, t_reactor_tid, ws_handle);
  return 1;
}

void h2_mp4push_on_stream_close(h2_conn_t *conn, int32_t stream_id) {
  for (int i = 0; i < H2_MP4PUSH_MAX; i++) {
    h2_mp4push_stream_t *tcs = &conn->mp4push[i];
    if (tcs->sid != stream_id) continue;
    int sub = tcs->sub_idx;
    tcs->sid = 0;
    tcs->sub_idx = -1;
    mp4push_sub_close(sub);
    return;
  }
}

#endif /* HAVE_HTTP2 */
