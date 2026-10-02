/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TEST_H2_CLIENT_H
#define DIPIXY_TEST_H2_CLIENT_H

#include <check.h>
#include <nghttp2/nghttp2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "client_resp.h"

#define H2C_IO_BUF 65536
#define H2C_FRAME_HDR 9
#define H2C_STREAMS 6
#define H2C_UPLOAD_MAX 2048

typedef struct {
  nghttp2_session *cli;
  client_resp_t streams[H2C_STREAMS];
  uint8_t upload[H2C_UPLOAD_MAX];
  size_t upload_len;
  size_t upload_off;
  int goaway;
  uint32_t goaway_code;
  int rst_count;
  int32_t rst_sid;
  uint32_t rst_code;
} h2c_t;

typedef struct {
  uint8_t type;
  int32_t sid;
  uint32_t code;
} h2c_frame_t;

static inline client_resp_t *h2c_stream_for(h2c_t *h, int32_t sid) {
  for (size_t i = 0; i < H2C_STREAMS; i++) {
    if (h->streams[i].sid == sid) return &h->streams[i];
  }
  for (size_t i = 0; i < H2C_STREAMS; i++) {
    if (!h->streams[i].sid) {
      h->streams[i].sid = sid;
      return &h->streams[i];
    }
  }
  return NULL;
}

static inline int h2c_on_header(nghttp2_session *s, const nghttp2_frame *f, const uint8_t *name, size_t nl, const uint8_t *val, size_t vl, uint8_t flags, void *ud) {
  h2c_t *h = ud;
  client_resp_t *st = h2c_stream_for(h, f->hd.stream_id);

  (void)s;
  (void)flags;
  if (st) client_resp_header(st, name, nl, val, vl);
  return 0;
}

static inline int h2c_on_data(nghttp2_session *s, uint8_t flags, int32_t sid, const uint8_t *data, size_t len, void *ud) {
  h2c_t *h = ud;
  client_resp_t *st = h2c_stream_for(h, sid);

  (void)s;
  (void)flags;
  if (st) client_resp_data(st, data, len);
  return 0;
}

static inline int h2c_on_close(nghttp2_session *s, int32_t sid, uint32_t code, void *ud) {
  h2c_t *h = ud;
  client_resp_t *st = h2c_stream_for(h, sid);

  (void)s;
  if (st) {
    st->closed = 1;
    st->close_code = code;
  }
  return 0;
}

static inline int h2c_on_frame(nghttp2_session *s, const nghttp2_frame *f, void *ud) {
  h2c_t *h = ud;

  (void)s;
  if (f->hd.type == NGHTTP2_GOAWAY) {
    h->goaway = 1;
    h->goaway_code = f->goaway.error_code;
  }
  if (f->hd.type == NGHTTP2_RST_STREAM) {
    h->rst_count++;
    h->rst_sid = f->hd.stream_id;
    h->rst_code = f->rst_stream.error_code;
  }
  if ((f->hd.flags & NGHTTP2_FLAG_END_STREAM) && (f->hd.type == NGHTTP2_HEADERS || f->hd.type == NGHTTP2_DATA)) {
    client_resp_t *st = h2c_stream_for(h, f->hd.stream_id);

    if (st) st->ended = 1;
  }
  return 0;
}

static inline void h2c_init(h2c_t *h) {
  nghttp2_session_callbacks *cbs;

  memset(h, 0, sizeof *h);
  ck_assert_int_eq(nghttp2_session_callbacks_new(&cbs), 0);
  nghttp2_session_callbacks_set_on_header_callback(cbs, h2c_on_header);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cbs, h2c_on_data);
  nghttp2_session_callbacks_set_on_stream_close_callback(cbs, h2c_on_close);
  nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, h2c_on_frame);
  ck_assert_int_eq(nghttp2_session_client_new(&h->cli, cbs, h), 0);
  nghttp2_session_callbacks_del(cbs);
  ck_assert_int_eq(nghttp2_submit_settings(h->cli, NGHTTP2_FLAG_NONE, NULL, 0), 0);
}

static inline void h2c_free(h2c_t *h) {
  nghttp2_session_del(h->cli);
}

static inline size_t h2c_take(h2c_t *h, uint8_t *buf, size_t cap) {
  size_t total = 0;
  const uint8_t *out;
  ssize_t n;

  while ((n = nghttp2_session_mem_send(h->cli, &out)) > 0) {
    ck_assert_uint_le(total + (size_t)n, cap);
    memcpy(buf + total, out, (size_t)n);
    total += (size_t)n;
  }
  ck_assert_int_ge((int)n, 0);
  return total;
}

static inline void h2c_feed(h2c_t *h, const uint8_t *data, size_t len) {
  ck_assert_int_ge((int)nghttp2_session_mem_recv(h->cli, data, len), 0);
}

static inline ssize_t h2c_upload_cb(nghttp2_session *s, int32_t sid, uint8_t *buf, size_t length, uint32_t *flags, nghttp2_data_source *src, void *ud) {
  h2c_t *h = ud;
  size_t n = h->upload_len - h->upload_off;

  (void)s;
  (void)sid;
  (void)src;
  if (!n) {
    *flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
    return NGHTTP2_ERR_DEFERRED;
  }
  if (n > length) n = length;
  memcpy(buf, h->upload + h->upload_off, n);
  h->upload_off += n;
  *flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
  return (ssize_t)n;
}

static inline int32_t h2c_connect(h2c_t *h, const char *path, const char *protocol) {
  nghttp2_nv nva[] = {
      {(uint8_t *)":method", (uint8_t *)"CONNECT", 7, 7, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":authority", (uint8_t *)"localhost", 10, 9, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":protocol", (uint8_t *)protocol, 9, strlen(protocol), NGHTTP2_NV_FLAG_NONE},
  };
  nghttp2_data_provider dp;
  int32_t sid;

  dp.read_callback = h2c_upload_cb;
  dp.source.ptr = h;
  sid = nghttp2_submit_request(h->cli, NULL, nva, 5, &dp, NULL);
  ck_assert_int_gt(sid, 0);
  return sid;
}

static inline void h2c_upload(h2c_t *h, int32_t sid, const uint8_t *data, size_t len) {
  ck_assert_uint_le(h->upload_len + len, sizeof h->upload);
  memcpy(h->upload + h->upload_len, data, len);
  h->upload_len += len;
  ck_assert_int_eq(nghttp2_session_resume_data(h->cli, sid), 0);
}

static inline int32_t h2c_request(h2c_t *h, const char *method, const char *path, const nghttp2_nv *extra, size_t n_extra) {
  nghttp2_nv nva[12] = {
      {(uint8_t *)":method", (uint8_t *)method, 7, strlen(method), NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":scheme", (uint8_t *)"https", 7, 5, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":authority", (uint8_t *)"localhost", 10, 9, NGHTTP2_NV_FLAG_NONE},
      {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE},
  };
  size_t n = 4;
  int32_t sid;

  for (size_t i = 0; i < n_extra; i++) nva[n++] = extra[i];
  sid = nghttp2_submit_request(h->cli, NULL, nva, n, NULL, NULL);
  ck_assert_int_gt(sid, 0);
  return sid;
}

static inline size_t h2c_raw_frame(uint8_t *out, uint8_t type, uint8_t flags, int32_t sid, const uint8_t *payload, size_t len) {
  out[0] = (uint8_t)(len >> 16);
  out[1] = (uint8_t)(len >> 8);
  out[2] = (uint8_t)len;
  out[3] = type;
  out[4] = flags;
  out[5] = (uint8_t)(sid >> 24);
  out[6] = (uint8_t)(sid >> 16);
  out[7] = (uint8_t)(sid >> 8);
  out[8] = (uint8_t)sid;
  if (len) memcpy(out + H2C_FRAME_HDR, payload, len);
  return H2C_FRAME_HDR + len;
}

static inline size_t h2c_raw_headers(uint8_t *out, int32_t sid, const nghttp2_nv *nva, size_t n) {
  nghttp2_hd_deflater *d;
  uint8_t block[1024];
  ssize_t blen;

  ck_assert_int_eq(nghttp2_hd_deflate_new(&d, 4096), 0);
  blen = nghttp2_hd_deflate_hd(d, block, sizeof block, nva, n);
  ck_assert_int_gt((int)blen, 0);
  nghttp2_hd_deflate_del(d);
  return h2c_raw_frame(out, NGHTTP2_HEADERS, NGHTTP2_FLAG_END_HEADERS | NGHTTP2_FLAG_END_STREAM, sid, block, (size_t)blen);
}

static inline size_t h2c_scan_frames(const uint8_t *buf, size_t n, h2c_frame_t *out, size_t cap) {
  size_t pos = 0;
  size_t count = 0;

  while (pos + H2C_FRAME_HDR <= n && count < cap) {
    size_t len = ((size_t)buf[pos] << 16) | ((size_t)buf[pos + 1] << 8) | buf[pos + 2];
    const uint8_t *p = buf + pos + H2C_FRAME_HDR;

    out[count].type = buf[pos + 3];
    out[count].sid = (int32_t)(((uint32_t)buf[pos + 5] << 24) | ((uint32_t)buf[pos + 6] << 16) | ((uint32_t)buf[pos + 7] << 8) | buf[pos + 8]) & 0x7FFFFFFF;
    out[count].code = 0;
    if (out[count].type == NGHTTP2_RST_STREAM && len >= 4) out[count].code = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    if (out[count].type == NGHTTP2_GOAWAY && len >= 8) out[count].code = ((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7];
    count++;
    pos += H2C_FRAME_HDR + len;
  }
  return count;
}

#endif
