/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../core/metrics.h"

#include "lib/helper/ioutil.h"

#include <string.h>
#include <strings.h>

const char RESP_400[] = "400 Bad Request";
const char RESP_401[] = "401 Unauthorized";
const char RESP_404[] = "404 Not Found";
const char RESP_405[] = "405 Method Not Allowed";
const char RESP_431[] = "431 Request Header Fields Too Large";
const char RESP_501[] = "501 Not Implemented";

void dispatch_sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  if (cap) buf[0] = '\0';
}

void dispatch_sb_add(strbuf_t *b, const char *s) {
  size_t n = bufcpy(b->buf + b->len, b->cap - b->len, s);
  size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
  b->len += n < room ? n : room;
}

void dispatch_sb_add_u64(strbuf_t *b, uint64_t v) {
  char tmp[20], rev[21];
  size_t n = 0, i;
  if (!v) {
    tmp[n++] = '0';
  } else {
    while (v) {
      tmp[n++] = (char)('0' + v % 10);
      v /= 10;
    }
  }
  for (i = 0; i < n; i++)
    rev[i] = tmp[n - 1 - i];
  rev[n] = '\0';
  dispatch_sb_add(b, rev);
}

void respond_status(conn_t *c, const char *status, int keep_alive) {
  char hdr[128];
  strbuf_t b;
  dispatch_sb_init(&b, hdr, sizeof hdr);
  dispatch_sb_add(&b, "HTTP/1.1 ");
  dispatch_sb_add(&b, status);
  dispatch_sb_add(&b, "\r\nConnection: ");
  dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
  dispatch_sb_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
  dipixy_metrics_note_http_error(); /* every respond_status() call is an error path */
}

void respond_401(conn_t *c, int keep_alive) {
  char hdr[160];
  strbuf_t b;
  dispatch_sb_init(&b, hdr, sizeof hdr);
  dispatch_sb_add(&b, "HTTP/1.1 ");
  dispatch_sb_add(&b, RESP_401);
  dispatch_sb_add(&b, "\r\nWWW-Authenticate: Basic realm=\"dipixy\"\r\nConnection: ");
  dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
  dispatch_sb_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->keep_alive = keep_alive ? 1 : 0;
  c->close_after_flush = keep_alive ? 0 : 1;
  dipixy_metrics_note_http_error();
}

/* content_type NULL: omit that header */
size_t build_ok_header(char *hdr, size_t hdrsz, const char *content_type, size_t body_len, int keep_alive) {
  strbuf_t b;
  dispatch_sb_init(&b, hdr, hdrsz);
  dispatch_sb_add(&b, "HTTP/1.1 200 OK\r\n");
  if (content_type) {
    dispatch_sb_add(&b, "Content-Type: ");
    dispatch_sb_add(&b, content_type);
    dispatch_sb_add(&b, "\r\n");
  }
  dispatch_sb_add(&b, "Content-Length: ");
  dispatch_sb_add_u64(&b, (uint64_t)body_len);
  dispatch_sb_add(&b, "\r\nConnection: ");
  dispatch_sb_add(&b, keep_alive ? "keep-alive" : "close");
  dispatch_sb_add(&b, "\r\n\r\n");
  return b.len;
}

/* HTTP/1.1 default persistent unless Connection: close. HTTP/1.0 and older
   default close unless Connection: keep-alive */
int wants_keepalive(int minor_version, const struct phr_header *headers, size_t num_headers) {
  char conn[32];
  int has_conn = find_header(headers, num_headers, "Connection", conn, sizeof conn);
  if (has_conn) {
    if (!strcasecmp(conn, "close"))       return 0;
    if (!strcasecmp(conn, "keep-alive"))  return 1;
  }
  return minor_version == 1;
}
