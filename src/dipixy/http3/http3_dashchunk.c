/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"

nghttp3_ssize h3_dashchunk_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud) {
  (void)h3;
  (void)sid;
  (void)veccnt;
  (void)conn_ud;
  h3_req_t *r = stream_ud;
  const uint8_t *p;
  size_t n;
  if (!r || r->dashchunk_sub_idx < 0) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }
  if (dash_lldash_ring_errored(r->dashchunk_sub_idx)) {
    *pflags = NGHTTP3_DATA_FLAG_EOF;
    return 0;
  }
  p = dash_lldash_ring_peek(r->dashchunk_sub_idx, &n);
  if (!p) {
    if (dash_lldash_sub_finalized(r->dashchunk_sub_idx)) {
      *pflags = NGHTTP3_DATA_FLAG_EOF;
      return 0;
    }
    return NGHTTP3_ERR_WOULDBLOCK;
  }
  vec[0].base = (uint8_t *)p;
  vec[0].len = n;
  *pflags = NGHTTP3_DATA_FLAG_NONE;
  dash_lldash_ring_advance(r->dashchunk_sub_idx, n);
  return 1;
}

void h3_dashchunk_wake(int sub_idx) {
  h3_conn_t *c = dash_lldash_sub_h3c(sub_idx);
  int64_t sid = dash_lldash_sub_h3_sid(sub_idx);
  int fd;
  if (!c) return;
  fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
  if (fd < 0) return;
  nghttp3_conn_resume_stream(c->h3conn, sid);
  flush_tx(c, fd);
}

#endif /* HAVE_HTTP3 */
