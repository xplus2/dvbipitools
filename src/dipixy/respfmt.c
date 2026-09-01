/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "segstore_int.h"
#include "core/metrics.h"
#include "reactor/internal.h"
#include "version.h"

#include "lib/helper/ioutil.h"

#include <stdlib.h>
#include <string.h>

static void set_persistence(conn_t *c, int keep_alive) {
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
}

void hls_sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  if (cap) buf[0] = '\0';
}

void hls_sb_add(strbuf_t *b, const char *s) {
  size_t n = bufcpy(b->buf + b->len, b->cap - b->len, s);
  size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
  b->len += n < room ? n : room;
}

void hls_sb_add_hex2(strbuf_t *b, unsigned v) {
  static const char digits[] = "0123456789abcdef";
  char tmp[3];
  tmp[0] = digits[(v >> 4) & 0xf];
  tmp[1] = digits[v & 0xf];
  tmp[2] = '\0';
  hls_sb_add(b, tmp);
}

void hls_sb_add_u64(strbuf_t *b, uint64_t v) {
  char tmp[20], rev[21];
  size_t n = 0;
  if (!v) {
    tmp[n++] = '0';
  } else while (v) {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  }
  for (size_t i = 0; i < n; i++)
    rev[i] = tmp[n - 1 - i];
  rev[n] = '\0';
  hls_sb_add(b, rev);
}

/* every call site here is "404 Not Found": counts as an HTTP error */
void queue_status(conn_t *c, const char *status, int keep_alive) {
  char hdr[128];
  strbuf_t b;
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 ");
  hls_sb_add(&b, status);
  hls_sb_add(&b, "\r\nConnection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
  dipixy_metrics_note_http_error();
}

/* 304 never carries a body regardless of is_head, ETag repeated per RFC 9110 */
void queue_not_modified(conn_t *c, const char *etag, int keep_alive) {
  char hdr[192];
  strbuf_t b;
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 304 Not Modified\r\nETag: \"");
  hls_sb_add(&b, etag);
  hls_sb_add(&b, "\"\r\nConnection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
}

/* builds "Access-Control-Allow-Origin: ...\r\n[Vary: Origin\r\n]" or "" into out */
void cors_prepare(const char *origin_hdr, char *out, size_t outsz) {
  int vary;
  const char *val = cors_match(reactor_cfg(), origin_hdr, &vary);
  strbuf_t b;
  hls_sb_init(&b, out, outsz);
  if (val) {
    hls_sb_add(&b, "Access-Control-Allow-Origin: ");
    hls_sb_add(&b, val);
    hls_sb_add(&b, "\r\n");
    if (vary) hls_sb_add(&b, "Vary: Origin\r\n");
  }
}

void queue_m3u8(conn_t *c, const char *body, size_t body_len, int is_head, int keep_alive, const char *cors_hdr) {
  char hdr[448];
  strbuf_t b;
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: ");
  hls_sb_add_u64(&b, (uint64_t)body_len);
  hls_sb_add(&b, "\r\nCache-Control: no-cache, no-store, must-revalidate\r\n");
  hls_sb_add(&b, cors_hdr);
  hls_sb_add(&b, "Connection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  if (!is_head) conn_queue(c, body, body_len);
  set_persistence(c, keep_alive);
}

static void queue_segment_hdr(conn_t *c, size_t body_len, const char *content_type, const char *etag, int keep_alive, const char *cors_hdr) {
  char hdr[512];
  strbuf_t b;
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: ");
  hls_sb_add(&b, content_type);
  hls_sb_add(&b, "\r\nContent-Length: ");
  hls_sb_add_u64(&b, (uint64_t)body_len);
  hls_sb_add(&b, "\r\nETag: \"");
  hls_sb_add(&b, etag);
  hls_sb_add(&b, "\"\r\nCache-Control: max-age=10\r\n");
  hls_sb_add(&b, cors_hdr);
  hls_sb_add(&b, "Connection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
}

/* etag NULL: resource has none (never true for hls_serve's segment/init callers) */
void queue_segment(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type, const char *etag, int is_head, int keep_alive, const char *cors_hdr) {
  queue_segment_hdr(c, body_len, content_type, etag, keep_alive, cors_hdr);
  if (!is_head) conn_queue(c, body, body_len);
  set_persistence(c, keep_alive);
}

int hls_zc_eligible(const conn_t *c, size_t body_len, int is_head) {
  return !is_head && !c->ssl && body_len >= HLS_ZC_MIN_LEN;
}

/* hls_zc_eligible() ensure !is_head. body ring owned, ref by caller, released on confirm */
void queue_segment_zc(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type, const char *etag, int keep_alive, const char *cors_hdr) {
  queue_segment_hdr(c, body_len, content_type, etag, keep_alive, cors_hdr);
  conn_queue_zc(c, body, body_len, seg_buf_release_cb, (void *)body);
  set_persistence(c, keep_alive);
}

/* seq/size (part index too, for parts). stable at resource lifetime: segments/parts/init immutable */
void seg_etag(uint32_t seq, size_t size, char *out, size_t outsz) {
  strbuf_t b;
  hls_sb_init(&b, out, outsz);
  hls_sb_add_u64(&b, seq);
  hls_sb_add(&b, "-");
  hls_sb_add_u64(&b, (uint64_t)size);
}

void part_etag(uint32_t seq, int part, size_t size, char *out, size_t outsz) {
  strbuf_t b;
  hls_sb_init(&b, out, outsz);
  hls_sb_add_u64(&b, seq);
  hls_sb_add(&b, ".");
  hls_sb_add_u64(&b, (uint64_t)part);
  hls_sb_add(&b, "-");
  hls_sb_add_u64(&b, (uint64_t)size);
}

void init_etag(int gen, size_t size, char *out, size_t outsz) {
  strbuf_t b;
  hls_sb_init(&b, out, outsz);
  hls_sb_add(&b, "init");
  hls_sb_add_u64(&b, (uint64_t)gen);
  hls_sb_add(&b, "-");
  hls_sb_add_u64(&b, (uint64_t)size);
}

/* returns a type tag: "index"/"init"/"ts"/"m4s". NULL if fn matches none */
const char *hls_filename_ext(const char *fn) {
  const char *p;
  if (strcmp(fn, "index.m3u8") == 0)        return "index";
  if (strcmp(fn, "init.mp4") == 0)          return "init";
  if (strncmp(fn, "seg", 3) != 0)  return NULL;
  p = fn + 3;
  if (!*p) return NULL;
  while (*p >= '0' && *p <= '9') p++;
  if (!strcmp(p, ".ts"))                    return "ts";
  if (!strcmp(p, ".m4s"))                   return "m4s";
  return NULL;
}

void queue_mpd(conn_t *c, const char *body, size_t body_len, int is_head, int keep_alive, const char *cors_hdr) {
  char hdr[448];
  strbuf_t b;
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: application/dash+xml\r\nContent-Length: ");
  hls_sb_add_u64(&b, (uint64_t)body_len);
  hls_sb_add(&b, "\r\nCache-Control: no-cache, no-store, must-revalidate\r\n");
  hls_sb_add(&b, cors_hdr);
  hls_sb_add(&b, "Connection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  if (!is_head) conn_queue(c, body, body_len);
  set_persistence(c, keep_alive);
}

char *write_lit(char *dst, const char *lit, size_t len) {
  memcpy(dst, lit, len);
  return dst + len;
}

char *write_u32(char *dst, uint32_t v, int min_digits) {
  char tmp[10];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n < min_digits)
    tmp[n++] = '0';
  for (int i = 0; i < n; i++)
    dst[i] = tmp[n - 1 - i];
  return dst + n;
}

char *write_u64_gen(char *dst, uint64_t v, int min_digits) {
  char tmp[20];
  int n = 0;
  do {
    tmp[n++] = (char)('0' + v % 10);
    v /= 10;
  } while (v);
  while (n < min_digits)
    tmp[n++] = '0';
  for (int i = 0; i < n; i++)
    dst[i] = tmp[n - 1 - i];
  return dst + n;
}

/* assumes v >= 0: true for every caller here */
char *write_fixed1(char *dst, double v) {
  uint32_t scaled = (uint32_t)(v * 10.0 + 0.5);
  dst = write_u32(dst, scaled / 10, 0);
  *dst++ = '.';
  return write_u32(dst, scaled % 10, 1);
}

/* assumes v >= 0: true for every caller here */
char *write_fixed3(char *dst, double v) {
  uint32_t scaled = (uint32_t)(v * 1000.0 + 0.5);
  dst = write_u32(dst, scaled / 1000, 0);
  *dst++ = '.';
  return write_u32(dst, scaled % 1000, 3);
}

void resp_set(hls_resp_t *out, int status, const char *content_type, const char *etag, const uint8_t *body, size_t body_len, int is_head) {
  out->status = status;
  out->content_type = content_type;
  if (etag)
    bufcpy(out->etag, sizeof out->etag, etag);
  else
    out->etag[0] = '\0';
  out->body_len = body_len;
  out->body = NULL;
  if (!body || is_head)
    return;
  out->body = malloc(body_len);
  if (!out->body) {
    out->status = 500;
    out->content_type = NULL;
    out->body_len = 0;
    return;
  }
  memcpy(out->body, body, body_len);
}

/* seg_buf-backed body: ref instead of copy. below HLS_ZC_MIN_LEN, falls back to resp_set() */
void resp_set_zc(hls_resp_t *out, int status, const char *content_type, const char *etag, uint8_t *body, size_t body_len, int is_head) {
  if (!body || is_head || body_len < HLS_ZC_MIN_LEN) {
    resp_set(out, status, content_type, etag, body, body_len, is_head);
    return;
  }
  out->status = status;
  out->content_type = content_type;
  if (etag)
    bufcpy(out->etag, sizeof out->etag, etag);
  else
    out->etag[0] = '\0';
  seg_buf_ref(body);
  out->body = body;
  out->body_len = body_len;
  out->zc = 1;
}

void hls_resp_body_release(uint8_t *body, int zc) {
  if (zc) {
    seg_buf_unref(body);
  } else {
    free(body);
  }
}
