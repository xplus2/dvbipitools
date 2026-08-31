/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../helper/log.h"
#include "priv.h"

static ssize_t body_read_raw(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->hpos < h->hlen) {
    size_t k = h->hlen - h->hpos;
    if (k > cap)
      k = cap;
    memcpy(buf, h->hold + h->hpos, k);
    h->hpos += k;
    return (ssize_t)k;
  }
  /* small cap: buffer via hold, 1 recv() not N (chunk framing bytes). big cap: bypass. */
  if (cap < sizeof h->hold) {
    ssize_t n = raw_recv(h, h->hold, sizeof h->hold, reason_out);
    size_t k;
    if (n <= 0)
      return n;
    h->hlen = (size_t)n;
    k = h->hlen < cap ? h->hlen : cap;
    memcpy(buf, h->hold, k);
    h->hpos = k;
    return (ssize_t)k;
  }
  return raw_recv(h, buf, cap, reason_out);
}

static ssize_t http_read_chunked(struct http *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->chunk_done)
    return 0;
  for (;;) {
    ssize_t n = body_read_raw(h, buf, cap, reason_out);
    size_t bufsz;
    ssize_t r;
    if (n <= 0)
      return n;
    bufsz = (size_t)n;
    r = phr_decode_chunked(&h->decoder, buf, &bufsz);
    if (r == -1) {
      log_line("http: malformed chunked body");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return -1;
    }
    if (r >= 0)
      h->chunk_done = 1;
    if (bufsz > 0 || h->chunk_done)
      return (ssize_t)bufsz;
  }
}

ssize_t http_read(http_t *h, void *buf, size_t cap, net_err_reason_t *reason_out) {
  if (h->chunked)
    return http_read_chunked(h, buf, cap, reason_out);
  return body_read_raw(h, buf, cap, reason_out);
}

int http_fd(const http_t *h) { return h->fd; }

void http_close(http_t *h) {
  if (!h)
    return;
  if (h->tls)
    tls_close(h->tls);
  else if (h->fd >= 0)
    close(h->fd);
  free(h);
}
