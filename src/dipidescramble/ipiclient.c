/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/psi/section_asm.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/signal.h"
#include "lib/net/httpclient/httpclient.h"

#include "ipiclient.h"
#include "version.h"

#define IPICLIENT_TOKEN_MAX 256
#define IPICLIENT_ETAG_MAX 128
#define IPICLIENT_TOKEN_HEADER_MAX 64
#define IPICLIENT_BODY_MAX (33 * PSI_SECTION_ASM_BUF_LEN) /* one EMM-U + up to 32 EMM-G, matches emmcache.c's cap */
#define IPICLIENT_POLL_TIMEOUT_S 20.0

struct ipiclient {
  http_url_t url;
  char token[IPICLIENT_TOKEN_MAX];
  char token_header[IPICLIENT_TOKEN_HEADER_MAX];
  int insecure;
  char etag[IPICLIENT_ETAG_MAX];
};

/* splits "scheme://token@host/path" into token + userinfo-stripped uri
   (http_url_parse() has no userinfo support). No '@': token empty, URI passed */
static int split_userinfo(const char *uri, char *token_out, size_t token_out_sz, char *uri_out, size_t uri_out_sz) {
  const char *scheme_end = strstr(uri, "://");
  const char *at;
  size_t scheme_len;

  if (!scheme_end)
    return -1;
  scheme_len = (size_t)(scheme_end - uri) + 3;
  at = strchr(scheme_end + 3, '@');
  if (!at) {
    if (bufcpy(uri_out, uri_out_sz, uri) >= uri_out_sz) return -1;
    token_out[0] = '\0';
    return 0;
  }

  {
    size_t tlen = (size_t)(at - (scheme_end + 3));
    size_t rest_len = scheme_len + strlen(at + 1);
    if (tlen >= token_out_sz || rest_len >= uri_out_sz) return -1;
    memcpy(token_out, scheme_end + 3, tlen);
    token_out[tlen] = '\0';
    memcpy(uri_out, uri, scheme_len);
    bufcpy(uri_out + scheme_len, uri_out_sz - scheme_len, at + 1);
  }
  return 0;
}

ipiclient_t *ipiclient_new(const char *uri, int insecure, const char *token_header) {
  ipiclient_t *c;
  char stripped[512];

  c = calloc(1, sizeof *c);
  if (!c) return NULL;
  if (split_userinfo(uri, c->token, sizeof c->token, stripped, sizeof stripped) != 0) {
    free(c);
    return NULL;
  }
  if (http_url_parse(stripped, &c->url) != 0) {
    free(c);
    return NULL;
  }
  if (!token_header || !token_header[0])
    token_header = "X-Device-Token";
  if (bufcpy(c->token_header, sizeof c->token_header, token_header) >= sizeof c->token_header) {
    free(c);
    return NULL;
  }
  c->insecure = insecure;
  return c;
}

void ipiclient_free(ipiclient_t *c) { free(c); }

typedef enum { IPI_FETCHING, IPI_READING_BODY } ipi_phase_t;

struct ipiclient_poll {
  ipi_phase_t phase;
  ipiclient_t *c;
  http_async_t *ha;
  http_t *h;
  double deadline;
  unsigned char body[IPICLIENT_BODY_MAX];
  size_t len;
};

ipiclient_poll_t *ipiclient_poll_start(ipiclient_t *c) {
  char hdr[IPICLIENT_TOKEN_HEADER_MAX + IPICLIENT_TOKEN_MAX + IPICLIENT_ETAG_MAX + 32];
  ipiclient_poll_t *p;

  if (c->etag[0])
    snprintf(hdr, sizeof hdr, "%s: %s\r\nIf-None-Match: %s", c->token_header, c->token, c->etag);
  else
    snprintf(hdr, sizeof hdr, "%s: %s", c->token_header, c->token);

  p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->c = c;
  p->deadline = mono_seconds() + IPICLIENT_POLL_TIMEOUT_S;
  p->ha = http_async_start(&c->url, TOOL_NAME "/" TOOL_VERSION, c->insecure, hdr, NULL);
  if (!p->ha) {
    free(p);
    return NULL;
  }
  p->phase = IPI_FETCHING;
  return p;
}

int ipiclient_poll_fd(const ipiclient_poll_t *p) { return p->phase == IPI_FETCHING ? http_async_poll_fd(p->ha) : http_fd(p->h); }

short ipiclient_poll_events(const ipiclient_poll_t *p) { return p->phase == IPI_FETCHING ? http_async_poll_events(p->ha) : POLLIN; }

ipiclient_poll_state_t ipiclient_poll_step(ipiclient_poll_t *p) {
  if (mono_seconds() > p->deadline) {
    log_line(TOOL_NAME ": -u/--unicast-emm fetch timed out");
    return IPICLIENT_POLL_ERROR;
  }

  if (p->phase == IPI_FETCHING) {
    http_async_state_t st = http_async_step(p->ha, NULL);
    const char *etag;
    if (st == HTTP_ASYNC_PENDING) return IPICLIENT_POLL_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(p->ha);
      p->ha = NULL;
      return IPICLIENT_POLL_ERROR; /* fetch failed, already logged by httpclient */
    }
    p->h = http_async_take(p->ha);
    p->ha = NULL;
    if (http_status(p->h) == 304) {
      http_close(p->h);
      p->h = NULL;
      return IPICLIENT_POLL_DONE;
    }
    etag = http_header(p->h, "etag");
    if (etag) bufcpy(p->c->etag, sizeof p->c->etag, etag);
    p->phase = IPI_READING_BODY;
  }

  for (;;) {
    ssize_t n = http_read(p->h, p->body + p->len, sizeof p->body - p->len, NULL);
    if (n < 0) {
      http_close(p->h);
      p->h = NULL;
      return IPICLIENT_POLL_DONE;
    }
    if (n == 0) return IPICLIENT_POLL_PENDING;
    p->len += (size_t)n;
    if (p->len >= sizeof p->body) {
      log_line(TOOL_NAME ": -u/--unicast-emm response too large, truncated");
      http_close(p->h);
      p->h = NULL;
      return IPICLIENT_POLL_DONE;
    }
  }
}

int ipiclient_poll_take(ipiclient_poll_t *p, emmcache_t *cache, device_state_t *d) {
  int changed = 0;
  size_t off = 0;

  while (off + 3 <= p->len) {
    size_t ulen = ((size_t)p->body[off + 1] << 8) | p->body[off + 2];
    if (off + 3 + ulen > p->len) break;
    if (emmcache_feed(cache, d, p->body + off + 3, ulen)) changed = 1;
    off += 3 + ulen;
  }
  free(p);
  return changed;
}

void ipiclient_poll_free(ipiclient_poll_t *p) {
  if (!p) return;
  if (p->ha) http_async_free(p->ha);
  if (p->h) http_close(p->h);
  free(p);
}
