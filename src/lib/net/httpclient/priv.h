/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_HTTPCLIENT_PRIV_H
#define DVBIPITOOLS_LIB_NET_HTTPCLIENT_PRIV_H

#include "../../vendor/picohttpparser/picohttpparser.h"
#include "../tls.h"
#include "httpclient.h"

#define HTTP_HDR_MAX 32
#define HTTP_PARSE_MAX_HEADERS 64
#define HTTP_REDIRECT_MAX 5
#define HTTP_CONNECT_TIMEOUT_MS 5000
#define HTTP_HEADER_TIMEOUT_MS 5000

struct http {
  int fd;
  tls_t *tls; /* NULL: plain http */
  http_url_t url;
  unsigned char hold[8192]; /* body read with headers, drained first */
  size_t hlen, hpos;
  int status;
  struct {
    char name[64];
    char value[512];
  } hdr[HTTP_HDR_MAX];
  int hdr_count;
  int chunked;
  int chunk_done;
  struct phr_chunked_decoder decoder;
};

typedef enum { HA_CONNECTING, HA_TLS_HANDSHAKE, HA_SENDING, HA_READING_HEADERS } http_async_phase_t;

struct http_async {
  http_async_phase_t phase;
  short want_events;
  netconnect_pending_t *connect_pending;
  http_url_t url; /* current hop, updated on redirect */
  char user_agent[128];
  int insecure;
  char extra_header[512];
  int have_extra_header;
  int redirects;
  struct http *h; /* reused (memset + refilled) across redirect hops */
  char reqbuf[2048];
  size_t req_len, req_sent;
  size_t hdr_got; /* bytes accumulated in h->hold while headers aren't complete yet */
};

/* url.c */
int resolve_location(http_url_t *u, const char *loc);

/* httpclient.c */
ssize_t raw_recv(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out);
int setup_transfer_encoding(struct http *h, net_err_reason_t *reason_out);
int build_get_request(char *buf, size_t cap, const http_url_t *url, const char *user_agent, const char *extra_header);
int http_is_redirect_status(int status);

int try_parse_response(struct http *h, size_t got, net_err_reason_t *reason_out);

#endif
