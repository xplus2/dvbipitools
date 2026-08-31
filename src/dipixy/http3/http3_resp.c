/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifdef HAVE_HTTP3

#include "../reactor/internal.h"
#include "http3.h"
#include "http3_int.h"

#include <string.h>

void h3_respond_status(h3_conn_t *c, int64_t sid, const char *status) {
  nghttp3_nv nva[] = {{(uint8_t *)":status", (uint8_t *)status, 7, strlen(status), NGHTTP3_NV_FLAG_NONE}};
  nghttp3_conn_submit_response(c->h3conn, sid, nva, 1, NULL);
}

void h3_respond_401(h3_conn_t *c, int64_t sid) {
  nghttp3_nv nva[] = {
      {(uint8_t *)":status", (uint8_t *)"401", 7, 3, NGHTTP3_NV_FLAG_NONE},
      {(uint8_t *)"www-authenticate", (uint8_t *)"Basic realm=\"dipixy\"", 16, 20, NGHTTP3_NV_FLAG_NONE},
  };
  nghttp3_conn_submit_response(c->h3conn, sid, nva, 2, NULL);
}

/* one-shot: nghttp3 vecs are zero-copy, no per-call length limit like nghttp2 */
static nghttp3_ssize h3_resp_read_cb(nghttp3_conn *h3, int64_t sid, nghttp3_vec *vec, size_t veccnt, uint32_t *pflags, void *conn_ud, void *stream_ud) {
  h3_req_t *r = stream_ud;
  (void)h3;
  (void)sid;
  (void)veccnt;
  (void)conn_ud;
  vec[0].base = r->resp_data + r->resp_off;
  vec[0].len = r->resp_len - r->resp_off;
  r->resp_off = r->resp_len;
  *pflags = NGHTTP3_DATA_FLAG_EOF;
  return 1;
}

/* hls_resp_t.status is always one of these */
static const char *h3_status_str(int status) {
  switch (status) {
    case 200: return "200";
    case 304: return "304";
    case 404: return "404";
    default: return "500";
  }
}

static size_t u64_to_dec(char *buf, uint64_t v) {
  char tmp[20];
  size_t n = 0;
  if (!v) {
    buf[0] = '0';
    return 1;
  }
  while (v) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (size_t i = 0; i < n; i++)
    buf[i] = tmp[n - 1 - i];
  return n;
}

/* body ownership transfers in: released by free_req() on stream close */
void h3_submit_resp(h3_conn_t *c, h3_req_t *r, int status, const char *content_type, const char *etag, size_t content_length, uint8_t *body, int zc, const char *origin_hdr) {
  const char *status_buf;
  char len_buf[24];
  size_t len_n;
  char etag_buf[56];
  nghttp3_nv nva[6];
  size_t nvlen = 0;
  nghttp3_data_reader dr;
  int cors_vary;
  const char *cors_val = cors_match(reactor_cfg(), origin_hdr, &cors_vary);

  status_buf = h3_status_str(status);
  nva[nvlen++] = (nghttp3_nv){(uint8_t *)":status", (uint8_t *)status_buf, 7, 3, NGHTTP3_NV_FLAG_NONE};
  if (content_type)
    nva[nvlen++] = (nghttp3_nv){(uint8_t *)"content-type", (uint8_t *)content_type, 12, strlen(content_type), NGHTTP3_NV_FLAG_NONE};
  len_n = u64_to_dec(len_buf, content_length);
  nva[nvlen++] = (nghttp3_nv){(uint8_t *)"content-length", (uint8_t *)len_buf, 14, len_n, NGHTTP3_NV_FLAG_NONE};
  if (etag && etag[0]) {
    size_t elen = strlen(etag);
    if (elen > sizeof etag_buf - 2) elen = sizeof etag_buf - 2; /* local clamp: independent of hls_resp_t.etag's own size */
    etag_buf[0] = '"';
    memcpy(etag_buf + 1, etag, elen);
    etag_buf[1 + elen] = '"';
    nva[nvlen++] = (nghttp3_nv){(uint8_t *)"etag", (uint8_t *)etag_buf, 4, elen + 2, NGHTTP3_NV_FLAG_NONE};
  }
  if (cors_val) {
    nva[nvlen++] = (nghttp3_nv){(uint8_t *)"access-control-allow-origin", (uint8_t *)cors_val, 28, strlen(cors_val),NGHTTP3_NV_FLAG_NONE};
    if (cors_vary) nva[nvlen++] = (nghttp3_nv){(uint8_t *)"vary", (uint8_t *)"Origin", 4, 6, NGHTTP3_NV_FLAG_NONE};
  }
  if (!body) {
    nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, nvlen, NULL);
    return;
  }
  r->resp_data = body;
  r->resp_len = content_length;
  r->resp_off = 0;
  r->resp_zc = zc;
  dr.read_data = h3_resp_read_cb;
  nghttp3_conn_set_stream_user_data(c->h3conn, r->stream_id, r);
  nghttp3_conn_submit_response(c->h3conn, r->stream_id, nva, nvlen, &dr);
}

void h3_respond_hls(h3_conn_t *c, h3_req_t *r, int handled, const hls_resp_t *resp) {
  if (!handled)
    h3_respond_status(c, r->stream_id, "404");
  else
    h3_submit_resp(c, r, resp->status, resp->content_type, resp->etag, resp->body_len, resp->body, resp->zc, r->origin[0] ? r->origin : NULL);
}

#endif /* HAVE_HTTP3 */
