/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/psi/section_asm.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/net/httpclient/httpclient.h"

#include "ipiclient.h"
#include "version.h"

#define IPICLIENT_TOKEN_MAX 256
#define IPICLIENT_ETAG_MAX 128
#define IPICLIENT_STALL_MAX 3 /* recv() timeouts to tolerate before giving up on one poll */
#define IPICLIENT_BODY_MAX (33 * PSI_SECTION_ASM_BUF_LEN) /* one EMM-U + up to 32 EMM-G, matches emmcache.c's cap */

struct ipiclient {
  http_url_t url;
  char token[IPICLIENT_TOKEN_MAX];
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
    if (bufcpy(uri_out, uri_out_sz, uri) >= uri_out_sz)
      return -1;
    token_out[0] = '\0';
    return 0;
  }

  {
    size_t tlen = (size_t)(at - (scheme_end + 3));
    size_t rest_len = scheme_len + strlen(at + 1);
    if (tlen >= token_out_sz || rest_len >= uri_out_sz)
      return -1;
    memcpy(token_out, scheme_end + 3, tlen);
    token_out[tlen] = '\0';
    memcpy(uri_out, uri, scheme_len);
    bufcpy(uri_out + scheme_len, uri_out_sz - scheme_len, at + 1);
  }
  return 0;
}

ipiclient_t *ipiclient_new(const char *uri, int insecure) {
  ipiclient_t *c;
  char stripped[512];

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  if (split_userinfo(uri, c->token, sizeof c->token, stripped, sizeof stripped) != 0) {
    free(c);
    return NULL;
  }
  if (http_url_parse(stripped, &c->url) != 0) {
    free(c);
    return NULL;
  }
  c->insecure = insecure;
  return c;
}

void ipiclient_free(ipiclient_t *c) { free(c); }

int ipiclient_poll(ipiclient_t *c, emmcache_t *cache, device_state_t *d) {
  char hdr[IPICLIENT_TOKEN_MAX + IPICLIENT_ETAG_MAX + 64];
  http_t *h;
  static unsigned char body[IPICLIENT_BODY_MAX];
  size_t len = 0, off;
  int changed = 0, stalls = 0;
  const char *etag;

  /* todo: configurabvle token name */
  if (c->etag[0])
    snprintf(hdr, sizeof hdr, "X-Device-Token: %s\r\nIf-None-Match: %s", c->token, c->etag);
  else
    snprintf(hdr, sizeof hdr, "X-Device-Token: %s", c->token);

  h = http_get(&c->url, TOOL_NAME "/" TOOL_VERSION, c->insecure, hdr, NULL);
  if (!h)
    return 0; /* fetch failed, already logged by httpclient */

  if (http_status(h) == 304) {
    http_close(h);
    return 0;
  }

  etag = http_header(h, "etag");
  if (etag)
    bufcpy(c->etag, sizeof c->etag, etag);

  for (;;) {
    ssize_t n = http_read(h, body + len, sizeof body - len, NULL);
    if (n < 0)
      break;
    if (n == 0) {
      if (++stalls > IPICLIENT_STALL_MAX) {
        log_line(TOOL_NAME ": -u/--unicast-emm response timed out");
        break;
      }
      continue;
    }
    stalls = 0;
    len += (size_t)n;
    if (len >= sizeof body) {
      log_line(TOOL_NAME ": -u/--unicast-emm response too large, truncated");
      break;
    }
  }
  http_close(h);
  off = 0;
  while (off + 3 <= len) {
    size_t ulen = ((size_t)body[off + 1] << 8) | body[off + 2];
    if (off + 3 + ulen > len)
      break;
    if (emmcache_feed(cache, d, body + off + 3, ulen))
      changed = 1;
    off += 3 + ulen;
  }
  return changed;
}
