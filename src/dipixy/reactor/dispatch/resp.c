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

void respond_status(conn_t *c, const char *status, int keep_alive) {
  char hdr[128];
  sbuf_t b;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 ");
  sbuf_add(&b, status);
  sbuf_add(&b, "\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
  dipixy_metrics_note_http_error(); /* every respond_status() call is an error path */
}

void respond_401(conn_t *c, int keep_alive) {
  char hdr[160];
  sbuf_t b;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 ");
  sbuf_add(&b, RESP_401);
  sbuf_add(&b, "\r\nWWW-Authenticate: Basic realm=\"dipixy\"\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\nContent-Length: 0\r\n\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
  dipixy_metrics_note_http_error();
}

/* content_type NULL: omit that header */
size_t build_ok_header(char *hdr, size_t hdrsz, const char *content_type, size_t body_len, int keep_alive) {
  sbuf_t b;
  sbuf_init(&b, hdr, hdrsz);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\n");
  if (content_type) {
    sbuf_add(&b, "Content-Type: ");
    sbuf_add(&b, content_type);
    sbuf_add(&b, "\r\n");
  }
  sbuf_add(&b, "Content-Length: ");
  sbuf_add_u64(&b, (uint64_t)body_len);
  sbuf_add(&b, "\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\n\r\n");
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
