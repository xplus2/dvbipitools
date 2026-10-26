/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _DEFAULT_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/reactor/dispatch/priv.h"

static void reactor_read_once(int epfd, conn_t *c) {
  const char *method;
  const char *path;
  size_t method_len;
  size_t path_len;
  int minor_version;
  struct phr_header headers[REACTOR_MAX_HEADERS];
  size_t num_headers;
  int pret;
  size_t body_len;
  body_framing_t framing;
  for (;;) {
    size_t room;
    ssize_t n;
    if (c->in.len >= REACTOR_MAX_REQ - 1) break; /* full: parse first, 431 only if incomplete */
    if (conn_in_reserve(c, 4096 + 1) < 0) { /* +1: room for NUL after fill */
      reactor_close(epfd, c);
      return;
    }
    room = c->in.cap - c->in.len - 1;
    n = tls_net_recv(c->fd, c->in.buf + c->in.len, room);
    if (n > 0) {
      c->in.len += (size_t)n;
      continue;
    }
    if (n == 0) {
      atomic_store_explicit(&c->read_done, 1, memory_order_relaxed); /* half-close: serve what is buffered */
      break;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    reactor_close(epfd, c);
    return;
  }

  c->in.buf[c->in.len] = '\0';
  num_headers = REACTOR_MAX_HEADERS;
  pret = phr_parse_request((const char *)c->in.buf, c->in.len, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, 0);
  if (pret == -2) {
    if (c->in.len >= REACTOR_MAX_REQ - 1) {
      respond_status(c, RESP_431, 0);
      c->state = CONN_WRITING;
      reactor_finish(epfd, c);
      return;
    }
    if (atomic_load_explicit(&c->read_done, memory_order_relaxed)) reactor_close(epfd, c);
    else reactor_arm(epfd, c, t_tls_want_write);
    return;
  }
  if (pret == -1) {
    respond_status(c, RESP_400, 0);
    c->state = CONN_WRITING;
    reactor_finish(epfd, c);
    return;
  }
  framing = request_body_len(headers, num_headers, &body_len);
  if (framing != BODY_FRAMING_OK) {
    respond_status(c, framing == BODY_FRAMING_BAD ? RESP_400 : RESP_501, 0);
    c->state = CONN_WRITING;
    reactor_finish(epfd, c);
    return;
  }
  if (body_len > 0) {
    size_t need = (size_t)pret + body_len;
    if (need > REACTOR_MAX_REQ - 1) {
      respond_status(c, RESP_400, 0);
      c->state = CONN_WRITING;
      reactor_finish(epfd, c);
      return;
    }
    if (c->in.len < need) {
      if (atomic_load_explicit(&c->read_done, memory_order_relaxed)) reactor_close(epfd, c);
      else reactor_arm(epfd, c, t_tls_want_write);
      return;
    }
  }
  /* request complete: parked dispatch not idle */
  atomic_store_explicit(&c->last_active, conn_now_s(), memory_order_relaxed);
  reactor_dispatch(epfd, c, method, method_len, path, path_len, minor_version, headers, num_headers, (size_t)pret, body_len);
}

static _Thread_local conn_t *t_read_conn;
static _Thread_local conn_t *t_read_again;
static _Thread_local int t_read_iters;

#define REACTOR_PIPELINE_BURST 32

void reactor_read(int epfd, conn_t *c) {
  conn_t *outer = t_read_conn;
  int outer_iters = t_read_iters;
  t_read_conn = c;
  t_read_iters = 0;
  do {
    t_read_again = NULL;
    reactor_read_once(epfd, c);
  } while (t_read_again == c); /* set by keepalive as last act, c alive */
  t_read_again = NULL;
  t_read_conn = outer;
  t_read_iters = outer_iters;
}

void reactor_keepalive(int epfd, conn_t *c) {
  size_t leftover = c->in.len > c->req_bytes ? c->in.len - c->req_bytes : 0;
  if (leftover) memmove(c->in.buf, c->in.buf + c->req_bytes, leftover);
  c->in.off = 0;
  c->in.len = leftover;
  c->out.off = 0;
  c->out.len = 0;
  c->keep_alive = 0;
  c->state = CONN_READING;
  reactor_arm(epfd, c, 0);
  if (!leftover) return;
  /* pipelined buffered B, dispatch without waiting for other sock reads. loop in reactor_read, no recursion */
  if (t_read_conn == c) {
    if (++t_read_iters < REACTOR_PIPELINE_BURST) t_read_again = c;
    else reactor_arm(epfd, c, 1); /* yield: EPOLLOUT wakes reactor_read for buffered requests */
  } else reactor_read(epfd, c);
}
