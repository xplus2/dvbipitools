/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_HTTPCLIENT_PRIV_H
#define DVBIPITOOLS_LIB_NET_HTTPCLIENT_PRIV_H

#include "../tls.h"
#include "httpclient.h"

#define HTTP_HDR_MAX 32
#define HTTP_REDIRECT_MAX 5
#define HTTP_CONNECT_TIMEOUT_MS 5000
#define HTTP_HEADER_TIMEOUT_MS 5000

/* chunked transfer-coding (RFC 7230 4.1) decode state, driven byte-by-byte for
   framing (size line, CRLFs, trailer) and in bulk for chunk data itself */
typedef enum { CHUNK_SIZE_LINE, CHUNK_DATA, CHUNK_DATA_CRLF, CHUNK_TRAILER, CHUNK_DONE } chunk_state_t;

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
  chunk_state_t cstate;
  unsigned long long chunk_remaining;
  char sizeline[64];
  size_t sizeline_len;
  int sizeline_bad; /* size line exceeded sizeline[], reject at LF instead of using truncated value */
};

typedef enum { HA_CONNECTING, HA_TLS_HANDSHAKE, HA_SENDING, HA_READING_HEADERS } http_async_phase_t;

struct http_async {
  http_async_phase_t phase;
  short want_events;
  netconnect_pending_t *connect_pending;
  http_url_t url; /* current hop; redirects update this */
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
char *find_header_end(char *b, size_t n, size_t *termlen);
void parse_headers(struct http *h, char *block);
int setup_transfer_encoding(struct http *h, net_err_reason_t *reason_out);

#endif
