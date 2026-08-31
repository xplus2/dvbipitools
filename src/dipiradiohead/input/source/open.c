/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "../../version.h"
#include "../playlist.h"
#include "priv.h"

static ssize_t sniff_fill(http_t *h, unsigned char *buf, size_t cap, net_err_reason_t *reason_out) {
  size_t got = 0;
  int stalls = 0;

  while (got < cap && stalls < 3) {
    ssize_t n = http_read(h, buf + got, cap - got, reason_out);
    if (n < 0)
      return got > 0 ? (ssize_t)got : -1; /* connection close after a full small (e.g. playlist) body is not an error */
    if (n == 0) {
      stalls++;
      continue;
    }
    got += (size_t)n;
    stalls = 0;
  }
  return (ssize_t)got;
}

source_t *build_source(http_t *h, const unsigned char *sniff, size_t got, source_meta_cb cb, void *ctx) {
  source_t *s = calloc(1, sizeof *s);
  if (!s) {
    http_close(h);
    return NULL;
  }
  s->http = h;
  s->id3 = id3_new(cb, ctx);
  if (!s->id3) {
    http_close(h);
    free(s);
    return NULL;
  }
  {
    const char *metaint_hdr = http_header(h, "icy-metaint");
    size_t metaint = metaint_hdr ? strtoul(metaint_hdr, NULL, 10) : 0;
    if (metaint) {
      s->icy = icy_new(metaint, cb, ctx);
      if (!s->icy) {
        id3_free(s->id3);
        http_close(h);
        free(s);
        return NULL;
      }
    }
  }

  if (s->icy)
    s->buf_len = icy_feed(s->icy, sniff, got, s->buf, sizeof s->buf);
  else {
    s->buf_len = got < sizeof s->buf ? got : sizeof s->buf;
    memcpy(s->buf, sniff, s->buf_len);
  }
  return s;
}

source_t *source_open(const char *uri, int insecure, source_meta_cb cb, void *ctx, net_err_reason_t *reason_out) {
  char cur_uri[2048];

  bufcpy(cur_uri, sizeof cur_uri, uri);
  for (int hops = 0; hops < SRC_MAX_HOPS; hops++) {
    http_url_t u;
    http_t *h;
    unsigned char sniff[SRC_SNIFF_CAP];
    ssize_t got;
    char next[2048];
    if (http_url_parse(cur_uri, &u)) {
      log_line("source: invalid uri: %s", cur_uri);
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return NULL;
    }
    h = http_get(&u, TOOL_NAME "/" TOOL_VERSION, insecure, NULL, reason_out);
    if (!h)
      return NULL;

    if (reason_out)
      *reason_out = NET_ERR_TIMEOUT; /* default if sniff_fill stalls out without a harder error */
    got = sniff_fill(h, sniff, sizeof sniff, reason_out);
    if (got <= 0) {
      log_line("source: empty response from %s", cur_uri);
      http_close(h);
      return NULL;
    }

    if (playlist_extract(sniff, (size_t)got, next, sizeof next)) {
      http_close(h);
      bufcpy(cur_uri, sizeof cur_uri, next);
      continue;
    }

    if (reason_out)
      *reason_out = NET_ERR_OTHER; /* build_source() only fails on allocation */
    return build_source(h, sniff, (size_t)got, cb, ctx);
  }
  log_line("source: too many playlist redirects");
  if (reason_out)
    *reason_out = NET_ERR_FORMAT;
  return NULL;
}
