/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../log.h"
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
  unsigned char *out = buf;
  size_t produced = 0;

  while (produced < cap) {
    if (h->cstate == CHUNK_DONE) {
      if (produced)
        break;
      if (reason_out)
        *reason_out = NET_ERR_EOF;
      return -1;
    }

    if (h->cstate == CHUNK_DATA) {
      size_t want = cap - produced;
      ssize_t n;
      if ((unsigned long long)want > h->chunk_remaining)
        want = (size_t)h->chunk_remaining;
      n = body_read_raw(h, out + produced, want, reason_out);
      if (n < 0)
        return produced ? (ssize_t)produced : -1;
      if (n == 0)
        return produced ? (ssize_t)produced : 0;
      produced += (size_t)n;
      h->chunk_remaining -= (unsigned long long)n;
      if (h->chunk_remaining == 0)
        h->cstate = CHUNK_DATA_CRLF;
      continue;
    }

    { /* CHUNK_SIZE_LINE / CHUNK_DATA_CRLF / CHUNK_TRAILER: framing bytes, one at a time */
      unsigned char c;
      ssize_t n = body_read_raw(h, &c, 1, reason_out);
      if (n < 0)
        return produced ? (ssize_t)produced : -1;
      if (n == 0)
        return produced ? (ssize_t)produced : 0;

      if (h->cstate == CHUNK_DATA_CRLF) {
        if (c == '\n')
          h->cstate = CHUNK_SIZE_LINE;
        continue; /* drop CR (and any stray byte) until LF shows up */
      }

      if (h->cstate == CHUNK_SIZE_LINE) {
        if (c == '\n') {
          char *end;
          unsigned long long size;
          h->sizeline[h->sizeline_len] = '\0';
          errno = 0;
          size = strtoull(h->sizeline, &end, 16);
          if (h->sizeline_bad || end == h->sizeline || !isxdigit((unsigned char)h->sizeline[0]) ||
              (*end != '\0' && *end != ';') || errno == ERANGE) {
            log_line("http: malformed chunk size line");
            if (reason_out)
              *reason_out = NET_ERR_FORMAT;
            return -1;
          }
          h->chunk_remaining = size;
          h->sizeline_len = 0;
          h->sizeline_bad = 0;
          h->cstate = size == 0 ? CHUNK_TRAILER : CHUNK_DATA;
          continue;
        }
        if (c != '\r') {
          if (h->sizeline_len < sizeof h->sizeline - 1)
            h->sizeline[h->sizeline_len++] = (char)c;
          else
            h->sizeline_bad = 1;
        }
        continue;
      }

      /* CHUNK_TRAILER: optional trailer headers after 0-size chunk, ended by blank line */
      if (c == '\n') {
        if (h->sizeline_len == 0)
          h->cstate = CHUNK_DONE;
        else
          h->sizeline_len = 0;
      } else if (c != '\r') {
        h->sizeline_len = 1;
      }
    }
  }
  return (ssize_t)produced;
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
