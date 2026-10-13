/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../../altsvc.h"
#include "../../core/metrics.h"

#include "lib/sys/ioutil.h"

#include <string.h>
#include <strings.h>

const char RESP_400[] = "400 Bad Request";
const char RESP_401[] = "401 Unauthorized";
const char RESP_404[] = "404 Not Found";
const char RESP_405[] = "405 Method Not Allowed";
const char RESP_412[] = "412 Precondition Failed";
const char RESP_431[] = "431 Request Header Fields Too Large";
const char RESP_501[] = "501 Not Implemented";
const char RESP_503[] = "503 Service Unavailable";
const char RESP_504[] = "504 Gateway Timeout";

void respond_status(conn_t *c, const char *status, int keep_alive) {
  char hdr[192];
  sbuf_t b;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 ");
  sbuf_add(&b, status);
  sbuf_add(&b, "\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\nContent-Length: 0\r\n");
  if (status == RESP_503) sbuf_add(&b, "Retry-After: " REACTOR_RETRY_AFTER_S "\r\n");
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
  dipixy_metrics_note_http_error(); /* every respond_status() call is an error path */
}

void respond_401(conn_t *c, int keep_alive) {
  char hdr[224];
  sbuf_t b;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 ");
  sbuf_add(&b, RESP_401);
  sbuf_add(&b, "\r\nWWW-Authenticate: Basic realm=\"dipixy\"\r\nConnection: ");
  sbuf_add(&b, keep_alive ? "keep-alive" : "close");
  sbuf_add(&b, "\r\nContent-Length: 0\r\n");
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "\r\n");
  conn_queue(c, hdr, b.len);
  set_persistence(c, keep_alive);
  dipixy_metrics_note_http_error();
}

/* content_type NULL: omit that header */
size_t build_ok_header(char *hdr, size_t hdrsz, const char *content_type, size_t body_len, int keep_alive, int is_tls) {
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
  sbuf_add(&b, "\r\n");
  sbuf_add(&b, altsvc_h1_line(is_tls));
  sbuf_add(&b, "\r\n");
  return b.len;
}

/* unbounded body: framed by conn close */
size_t build_stream_header(char *hdr, size_t hdrsz, const char *content_type, int is_tls) {
  sbuf_t b;
  sbuf_init(&b, hdr, hdrsz);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\nContent-Type: ");
  sbuf_add(&b, content_type);
  sbuf_add(&b, "\r\nConnection: close\r\n");
  sbuf_add(&b, altsvc_h1_line(is_tls));
  sbuf_add(&b, "\r\n");
  return b.len;
}

/* HTTP/1.1 default persistent unless Connection has close token. HTTP/1.0 and
   older default close unless keep-alive token. Close wins over keep-alive */
int wants_keepalive(int minor_version, const struct phr_header *headers, size_t num_headers) {
  char conn[128];
  int keep = minor_version == 1;
  if (find_header(headers, num_headers, "Connection", conn, sizeof conn)) {
    char *save = NULL;
    for (char *tok = strtok_r(conn, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      while (*tok == ' ' || *tok == '\t') tok++;
      size_t n = strlen(tok);
      while (n && (tok[n - 1] == ' ' || tok[n - 1] == '\t')) tok[--n] = '\0';
      if (!strcasecmp(tok, "close")) return 0;
      if (!strcasecmp(tok, "keep-alive")) keep = 1;
    }
  }
  return keep;
}
