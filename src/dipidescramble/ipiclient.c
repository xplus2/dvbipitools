/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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

  if (!scheme_end) return -1;
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

struct ipiclient_poll {
  ipiclient_t *c;
  http_fetch_t *f;
  double deadline;
  unsigned char body[IPICLIENT_BODY_MAX];
  size_t len;
};

ipiclient_poll_t *ipiclient_poll_start(ipiclient_t *c) {
  char hdr[IPICLIENT_TOKEN_HEADER_MAX + IPICLIENT_TOKEN_MAX + 8];
  ipiclient_poll_t *p;

  snprintf(hdr, sizeof hdr, "%s: %s", c->token_header, c->token);
  p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->c = c;
  p->deadline = mono_seconds() + IPICLIENT_POLL_TIMEOUT_S;
  p->f = http_fetch_start(&c->url, TOOL_NAME "/" TOOL_VERSION, c->insecure, hdr, c->etag[0] ? c->etag : NULL, p->body, sizeof p->body, NULL, NULL);
  if (!p->f) {
    free(p);
    return NULL;
  }
  return p;
}

int ipiclient_poll_fd(const ipiclient_poll_t *p) { return http_fetch_poll_fd(p->f); }

short ipiclient_poll_events(const ipiclient_poll_t *p) { return http_fetch_poll_events(p->f); }

ipiclient_poll_state_t ipiclient_poll_step(ipiclient_poll_t *p) {
  http_fetch_state_t st;
  int status, truncated;
  char etag[IPICLIENT_ETAG_MAX];

  if (mono_seconds() > p->deadline) {
    log_line(TOOL_NAME ": -u/--unicast-emm fetch timed out");
    return IPICLIENT_POLL_ERROR;
  }
  st = http_fetch_step(p->f, NULL);
  if (st == HTTP_FETCH_PENDING) return IPICLIENT_POLL_PENDING;
  if (st == HTTP_FETCH_ERROR) {
    http_fetch_free(p->f);
    p->f = NULL;
    return IPICLIENT_POLL_ERROR; /* fetch failed, already logged by httpclient */
  }

  http_fetch_take(p->f, &p->len, &status, etag, sizeof etag, &truncated, NULL);
  p->f = NULL;
  if (status == 304) return IPICLIENT_POLL_DONE;
  if (etag[0]) bufcpy(p->c->etag, sizeof p->c->etag, etag);
  if (truncated) log_line(TOOL_NAME ": -u/--unicast-emm response too large, truncated");
  return IPICLIENT_POLL_DONE;
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
  if (p->f) http_fetch_free(p->f);
  free(p);
}
