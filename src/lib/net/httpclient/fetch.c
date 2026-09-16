/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "../../helper/ioutil.h"
#include "httpclient.h"

#define HTTP_FETCH_ETAG_MAX 128

typedef enum { HF_FETCHING, HF_READING } http_fetch_phase_t;

struct http_fetch {
  http_fetch_phase_t phase;
  http_async_t *ha;
  http_t *h;
  unsigned char *buf;
  size_t cap;
  size_t len;
  int status;
  int truncated;
  char etag[HTTP_FETCH_ETAG_MAX];
};

http_fetch_t *http_fetch_start(const http_url_t *url, const char *user_agent, int insecure, const char *extra_header, const char *etag_in,
                               unsigned char *buf, size_t cap, http_t *reuse, net_err_reason_t *reason_out) {
  char hdr[768];
  const char *hdr_ptr = extra_header;
  http_fetch_t *f;
  if (etag_in && etag_in[0]) {
    if (extra_header && extra_header[0]) snprintf(hdr, sizeof hdr, "%s\r\nIf-None-Match: %s", extra_header, etag_in);
    else snprintf(hdr, sizeof hdr, "If-None-Match: %s", etag_in);
    hdr_ptr = hdr;
  }
  f = calloc(1, sizeof *f);
  if (!f) {
    if (reuse) http_close(reuse);
    return NULL;
  }
  f->buf = buf;
  f->cap = cap;
  if (reuse) {
    const http_url_t *ru = http_final_url(reuse);
    if (ru->tls == url->tls && ru->port == url->port && !strcasecmp(ru->host, url->host))
      f->ha = http_async_start_reuse(reuse, url, user_agent, insecure, hdr_ptr, reason_out);
    else {
      http_close(reuse);
      f->ha = http_async_start(url, user_agent, insecure, hdr_ptr, reason_out);
    }
  } else {
    f->ha = http_async_start(url, user_agent, insecure, hdr_ptr, reason_out);
  }
  if (!f->ha) {
    free(f);
    return NULL;
  }
  f->phase = HF_FETCHING;
  return f;
}

int http_fetch_poll_fd(const http_fetch_t *f) { return f->phase == HF_FETCHING ? http_async_poll_fd(f->ha) : http_fd(f->h); }

short http_fetch_poll_events(const http_fetch_t *f) { return f->phase == HF_FETCHING ? http_async_poll_events(f->ha) : POLLIN; }

http_fetch_state_t http_fetch_step(http_fetch_t *f, net_err_reason_t *reason_out) {
  if (f->phase == HF_FETCHING) {
    http_async_state_t st = http_async_step(f->ha, reason_out);
    const char *etag;
    if (st == HTTP_ASYNC_PENDING) return HTTP_FETCH_PENDING;
    if (st == HTTP_ASYNC_ERROR) {
      http_async_free(f->ha);
      f->ha = NULL;
      return HTTP_FETCH_ERROR;
    }
    f->h = http_async_take(f->ha);
    f->ha = NULL;
    f->status = http_status(f->h);
    etag = http_header(f->h, "etag");
    if (etag) bufcpy(f->etag, sizeof f->etag, etag);
    if (f->status == 304) return HTTP_FETCH_DONE;
    f->phase = HF_READING;
  }

  for (;;) {
    ssize_t n = http_read(f->h, f->buf + f->len, f->cap - f->len, NULL);
    if (n < 0) return HTTP_FETCH_DONE;
    if (n == 0) return HTTP_FETCH_PENDING;
    f->len += (size_t)n;
    if (f->len >= f->cap) {
      f->truncated = 1;
      return HTTP_FETCH_DONE;
    }
  }
}

void http_fetch_take(http_fetch_t *f, size_t *len_out, int *status_out, char *etag_out, size_t etag_out_sz, int *truncated_out, http_t **reusable_out) {
  if (len_out) *len_out = f->len;
  if (status_out) *status_out = f->status;
  if (etag_out) bufcpy(etag_out, etag_out_sz, f->etag);
  if (truncated_out) *truncated_out = f->truncated;
  if (reusable_out) {
    if (f->h && http_can_reuse(f->h)) {
      *reusable_out = f->h;
      f->h = NULL;
    } else {
      *reusable_out = NULL;
    }
  }
  if (f->h) http_close(f->h);
  free(f);
}

void http_fetch_free(http_fetch_t *f) {
  if (!f) return;
  if (f->ha) http_async_free(f->ha);
  if (f->h) http_close(f->h);
  free(f);
}
