/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_HTTPCLIENT_H
#define DVBIPITOOLS_LIB_NET_HTTPCLIENT_H

#include <stddef.h>
#include <sys/types.h>

typedef struct http http_t;

typedef struct {
  int tls; /* nonzero: https */
  char host[256];
  unsigned port;
  char path[1024]; /* leading '/' */
} http_url_t;

/* "http://host[:port]/path" or "https://...". 0 ok, -1 bad uri */
int http_url_parse(const char *uri, http_url_t *u);

/* GET, sends Icy-MetaData: 1, follows up to 5 redirects. insecure skips TLS verification. NULL on failure (logged) */
http_t *http_get(const http_url_t *url, const char *user_agent, int insecure);

/* response header lookup, case-insensitive name. NULL if absent */
const char *http_header(const http_t *h, const char *name);

/* url actually fetched, post-redirects */
const http_url_t *http_final_url(const http_t *h);

/* body bytes. >0 read, 0 transient (timed out, caller should retry/poll), -1 closed/error */
ssize_t http_read(http_t *h, void *buf, size_t cap);

void http_close(http_t *h);

#endif
