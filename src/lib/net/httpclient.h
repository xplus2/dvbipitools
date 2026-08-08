/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_HTTPCLIENT_H
#define DVBIPITOOLS_LIB_NET_HTTPCLIENT_H

#include <stddef.h>
#include <sys/types.h>

#include "netconnect.h"

typedef struct http http_t;

typedef struct {
  int tls; /* nonzero: https */
  char host[256];
  unsigned port;
  char path[1024]; /* leading '/' */
} http_url_t;

/* "http://host[:port]/path" or "https://...". 0 ok, -1 bad uri */
int http_url_parse(const char *uri, http_url_t *u);

/* GET, sends Icy-MetaData: 1, follows up to 5 redirects. insecure skips TLS verification.
   extra_header: one raw "Name: value" line added to the request, no trailing CRLF - NULL for none.
   NULL on failure (logged). reason_out: nullable, set only on NULL return */
http_t *http_get(const http_url_t *url, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out);

/* response header lookup, case-insensitive name. NULL if absent */
const char *http_header(const http_t *h, const char *name);

/* HTTP status of the (possibly redirected-to) response. Includes 304 Not Modified
   when extra_header carried If-None-Match and the server confirmed no change.
   http_get() returns that too (body empty) instead of treating it as failure */
int http_status(const http_t *h);

/* url actually fetched, post-redirects */
const http_url_t *http_final_url(const http_t *h);

/* body bytes. >0 read, 0 transient (timed out, caller should retry/poll), -1 closed/error.
   reason_out: nullable, set only on -1 */
ssize_t http_read(http_t *h, void *buf, size_t cap, net_err_reason_t *reason_out);

/* underlying socket fd, for a caller's own poll(); valid for the life of h */
int http_fd(const http_t *h);

void http_close(http_t *h);

typedef struct http_async http_async_t;

typedef enum { HTTP_ASYNC_PENDING, HTTP_ASYNC_DONE, HTTP_ASYNC_ERROR } http_async_state_t;

/* async http_get(): never blocks, caller polls. same semantics (redirects, Icy-MetaData: 1).
   caller decides how long to poll before http_async_free() abandons it. no internal timeout.
   NULL on immediate setup failure (bad uri, resolve failure - logged).
   reason_out: nullable, set only on NULL return */
http_async_t *http_async_start(const http_url_t *url, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out);

/* what to poll() for right now */
int http_async_poll_fd(const http_async_t *a);
short http_async_poll_events(const http_async_t *a);

/* call after poll() readiness (harmless speculatively too - PENDING if nothing ready).
   reason_out: nullable, set only on HTTP_ASYNC_ERROR */
http_async_state_t http_async_step(http_async_t *a, net_err_reason_t *reason_out);

/* DONE only: hands over the http_t http_get() would've returned, frees the async handle */
http_t *http_async_take(http_async_t *a);

/* frees handle + owned state; safe at any state incl. PENDING */
void http_async_free(http_async_t *a);

#endif
