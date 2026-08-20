/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../ioutil.h"
#include "../../log.h"
#include "../netconnect.h"
#include "priv.h"

static ssize_t raw_send_nb(struct http *h, const char *buf, size_t n) {
  ssize_t w;

  if (h->tls)
    return tls_write(h->tls, buf, n); /* already 0-on-transient */
  w = send(h->fd, buf, n, MSG_NOSIGNAL);
  if (w < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return 0;
    log_line("send: %s", strerror(errno));
    return -1;
  }
  return w;
}

static int async_build_request(http_async_t *a) {
  int rl = build_get_request(a->reqbuf, sizeof a->reqbuf, &a->h->url, a->user_agent, a->have_extra_header ? a->extra_header : NULL);
  if (rl < 0)
    return -1;
  a->req_len = (size_t)rl;
  a->req_sent = 0;
  return 0;
}

/* (re)starts connect for a->url into a->h, fresh each hop (initial + every redirect) */
static int async_start_connect(http_async_t *a, net_err_reason_t *reason_out) {
  int fd = netconnect_tcp_start(a->url.host, a->url.port, &a->connect_pending, reason_out);
  if (fd < 0)
    return -1;
  memset(a->h, 0, sizeof *a->h);
  a->h->fd = fd;
  a->h->url = a->url;
  a->phase = HA_CONNECTING;
  a->want_events = POLLOUT;
  a->hdr_got = 0;
  return 0;
}

http_async_t *http_async_start(const http_url_t *url_in, const char *user_agent, int insecure, const char *extra_header, net_err_reason_t *reason_out) {
  http_async_t *a = calloc(1, sizeof *a);
  if (!a)
    return NULL;
  a->h = calloc(1, sizeof *a->h);
  if (!a->h) {
    free(a);
    return NULL;
  }
  a->url = *url_in;
  bufcpy(a->user_agent, sizeof a->user_agent, user_agent ? user_agent : "dvbipitools");
  a->insecure = insecure;
  if (extra_header) {
    bufcpy(a->extra_header, sizeof a->extra_header, extra_header);
    a->have_extra_header = 1;
  }
  if (async_start_connect(a, reason_out) != 0) {
    free(a->h);
    free(a);
    return NULL;
  }
  return a;
}

int http_async_poll_fd(const http_async_t *a) { return a->h->fd; }

short http_async_poll_events(const http_async_t *a) { return a->want_events; }

static http_async_state_t async_after_headers(http_async_t *a, net_err_reason_t *reason_out) {
  struct http *h = a->h;

  if (http_is_redirect_status(h->status)) {
    const char *loc;
    http_url_t next;
    if (a->redirects >= HTTP_REDIRECT_MAX) {
      log_line("http: too many redirects");
      if (reason_out)
        *reason_out = NET_ERR_HTTP;
      return HTTP_ASYNC_ERROR;
    }
    loc = http_header(h, "location");
    next = h->url;
    if (!loc || resolve_location(&next, loc) != 0) {
      log_line("http: redirect without usable Location");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return HTTP_ASYNC_ERROR;
    }
    if (h->tls) {
      tls_close(h->tls);
      h->tls = NULL;
    } else if (h->fd >= 0) {
      close(h->fd);
    }
    a->redirects++;
    a->url = next;
    if (async_start_connect(a, reason_out) != 0)
      return HTTP_ASYNC_ERROR;
    return HTTP_ASYNC_PENDING;
  }
  if (h->status == 304)
    return HTTP_ASYNC_DONE; /* not-modified: caller checks http_status(), body is empty */
  if (h->status < 200 || h->status >= 300) {
    log_line("http %d fetching %s%s", h->status, h->url.host, h->url.path);
    if (reason_out)
      *reason_out = NET_ERR_HTTP;
    return HTTP_ASYNC_ERROR;
  }
  return HTTP_ASYNC_DONE;
}

/* starts async TLS handshake. 0: pending (a->phase updated). -1: failed (reason_out set) */
static int start_tls(http_async_t *a, struct http *h, net_err_reason_t *reason_out) {
  h->tls = tls_connect_start(h->fd, h->url.host, a->insecure);
  if (!h->tls) {
    if (reason_out) /* already logged */
      *reason_out = NET_ERR_TLS;
    return -1;
  }
  a->phase = HA_TLS_HANDSHAKE;
  a->want_events = POLLOUT; /* SSL_connect()'s first move is normally outbound */
  return 0;
}

/* drains a->reqbuf into h. 1: fully sent. 0: pending (POLLOUT armed). -1: error (reason_out set) */
static int send_request_buf(http_async_t *a, struct http *h, net_err_reason_t *reason_out) {
  while (a->req_sent < a->req_len) {
    ssize_t n = raw_send_nb(h, a->reqbuf + a->req_sent, a->req_len - a->req_sent);
    if (n < 0) {
      if (reason_out)
        *reason_out = NET_ERR_READ;
      return -1;
    }
    if (n == 0) {
      a->want_events = POLLOUT;
      return 0;
    }
    a->req_sent += (size_t)n;
  }
  return 1;
}

/* returns 0 on header terminator found (term_out/termlen_out set), -1 on error, 1 = more data needed */
static int read_response_headers(struct http *h, http_async_t *a, net_err_reason_t *reason_out, char **term_out, size_t *termlen_out) {
  for (;;) {
    ssize_t n;
    if (a->hdr_got >= sizeof h->hold) {
      log_line("http: response headers too large");
      if (reason_out)
        *reason_out = NET_ERR_FORMAT;
      return -1;
    }
    n = raw_recv(h, h->hold + a->hdr_got, sizeof h->hold - a->hdr_got, reason_out);
    if (n < 0) {
      log_line("http: connection closed while reading headers");
      return -1;
    }
    if (n == 0) {
      a->want_events = POLLIN;
      return 1;
    }
    a->hdr_got += (size_t)n;
    *term_out = find_header_end((char *)h->hold, a->hdr_got, termlen_out);
    if (*term_out)
      return 0;
  }
}

http_async_state_t http_async_step(http_async_t *a, net_err_reason_t *reason_out) {
  struct http *h = a->h;

  switch (a->phase) {
    case HA_CONNECTING: {
      int cr = netconnect_tcp_finish(&a->connect_pending, &h->fd, reason_out);
      if (cr == 0)
        return HTTP_ASYNC_PENDING; /* this address failed, next candidate connecting on h->fd */
      if (cr < 0) {
        log_line("connect %s:%u: %s", h->url.host, h->url.port, strerror(errno));
        return HTTP_ASYNC_ERROR;
      }
      if (h->url.tls) {
        if (start_tls(a, h, reason_out) != 0)
          return HTTP_ASYNC_ERROR;
        return HTTP_ASYNC_PENDING;
      }
      if (async_build_request(a) != 0) {
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
        return HTTP_ASYNC_ERROR;
      }
      a->phase = HA_SENDING;
      a->want_events = POLLOUT;
      return HTTP_ASYNC_PENDING;
    }

    case HA_TLS_HANDSHAKE: {
      tls_handshake_status_t st = tls_handshake_step(h->tls);
      if (st == TLS_HANDSHAKE_WANT_READ) {
        a->want_events = POLLIN;
        return HTTP_ASYNC_PENDING;
      }
      if (st == TLS_HANDSHAKE_WANT_WRITE) {
        a->want_events = POLLOUT;
        return HTTP_ASYNC_PENDING;
      }
      if (st == TLS_HANDSHAKE_ERROR) {
        if (reason_out)
          *reason_out = NET_ERR_TLS;
        return HTTP_ASYNC_ERROR;
      }
      if (async_build_request(a) != 0) {
        if (reason_out)
          *reason_out = NET_ERR_FORMAT;
        return HTTP_ASYNC_ERROR;
      }
      a->phase = HA_SENDING;
      a->want_events = POLLOUT;
      return HTTP_ASYNC_PENDING;
    }

    case HA_SENDING: {
      int r = send_request_buf(a, h, reason_out);
      if (r < 0)
        return HTTP_ASYNC_ERROR;
      if (r == 0)
        return HTTP_ASYNC_PENDING;
      a->phase = HA_READING_HEADERS;
      a->want_events = POLLIN;
      return HTTP_ASYNC_PENDING;
    }

    case HA_READING_HEADERS: {
      char *term = NULL;
      size_t termlen = 0;
      int st = read_response_headers(h, a, reason_out, &term, &termlen);

      if (st < 0)
        return HTTP_ASYNC_ERROR;
      if (st > 0)
        return HTTP_ASYNC_PENDING;
      finish_headers(h, term, termlen, a->hdr_got);
      if (setup_transfer_encoding(h, reason_out) != 0)
        return HTTP_ASYNC_ERROR;
      return async_after_headers(a, reason_out);
    }
  }

  return HTTP_ASYNC_ERROR;
}

http_t *http_async_take(http_async_t *a) {
  struct http *h = a->h;
  free(a);
  return h;
}

void http_async_free(http_async_t *a) {
  if (!a)
    return;
  if (a->connect_pending)
    netconnect_tcp_abort(a->connect_pending);
  if (a->h) {
    if (a->h->tls)
      tls_close(a->h->tls);
    else if (a->h->fd >= 0)
      close(a->h->fd);
    free(a->h);
  }
  free(a);
}
