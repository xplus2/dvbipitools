/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/reactor.h"
#include "dipixy/reactor/reactor_tls.h"
#include "dipixy/version.h"
#include "lib/helper/log.h"

int conn_flush(conn_t *c, int epfd) {
  conn_zc_drain(c);
  for (;;) {
    int last_errno = 0;
    if (c->wire.off == c->wire.len) {
      pthread_mutex_lock(&c->out_lock);
      if (c->out.len == c->out.off) {
        c->out.off = 0;
        c->out.len = 0; /* reset under lock that saw it empty: concurrent queuers must not be wiped */
        pthread_mutex_unlock(&c->out_lock);
        c->wire.off = 0;
        c->wire.len = 0;
        atomic_store_explicit(&c->wire_pending, 0, memory_order_relaxed);
        break;
      }
      conn_buf drained = c->wire;
      c->wire = c->out;
      c->out = drained;
      c->out.off = 0;
      c->out.len = 0;
      atomic_store_explicit(&c->wire_pending, c->wire.len - c->wire.off, memory_order_relaxed);
      pthread_mutex_unlock(&c->out_lock);
    }
    while (c->wire.off < c->wire.len) {
      ssize_t n = tls_net_send(c->fd, c->wire.buf + c->wire.off, c->wire.len - c->wire.off);
      if (n > 0) {
        c->wire.off += (size_t)n;
        continue;
      }
      last_errno = errno;
      break;
    }
    atomic_store_explicit(&c->wire_pending, c->wire.len - c->wire.off, memory_order_relaxed);
    if (c->wire.off < c->wire.len) {
      /* tls_net_send maps SSL_WANT_READ/WRITE to EAGAIN. EPOLLIN stays armed: covers WANT_READ mid-renegotiation.
         EPOLLOUT added: full send buffer also wakes */
      if (last_errno == EAGAIN || last_errno == EWOULDBLOCK) {
        unsigned char want_expected = 0;
        if (atomic_compare_exchange_strong_explicit(&c->want_write, &want_expected, 1, memory_order_relaxed, memory_order_relaxed)) {
          if (conn_epoll_mod(c, epfd, 1) < 0) {
            atomic_store_explicit(&c->want_write, 0, memory_order_relaxed);
            return CONN_FLUSH_ERROR;
          }
        }
        atomic_store_explicit(&c->last_active, conn_now_s(), memory_order_relaxed);
        return CONN_FLUSH_MORE;
      }
      return CONN_FLUSH_ERROR;
    }
  }

  while (c->zc.active) {
    int used_zc = 0;
    ssize_t n = tls_net_send_zc(c->fd, c->zc.data + c->zc.off, c->zc.len - c->zc.off, &used_zc);
    if (n > 0) {
      c->zc.off += (size_t)n;
      if (used_zc) {
        c->zc.any_zc = 1;
        if (c->zc.zc_ids++ == 0) c->zc.first_zc_id = c->zc_next_id;
        c->zc_next_id++;
      }
      if (c->zc.off < c->zc.len) continue;
      /* fully handed off: release now if never pinned, else await confirm */
      if (!c->zc.any_zc) {
        c->zc.release(c->zc.release_arg);
      } else {
        c->zc_await[c->zc_await_count].id_lo = c->zc.first_zc_id;
        c->zc_await[c->zc_await_count].ids = c->zc.zc_ids;
        c->zc_await[c->zc_await_count].done = 0;
        c->zc_await[c->zc_await_count].release = c->zc.release;
        c->zc_await[c->zc_await_count].release_arg = c->zc.release_arg;
        c->zc_await_count++;
      }
      c->zc.active = 0;
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      unsigned char want_expected = 0;
      if (atomic_compare_exchange_strong_explicit(&c->want_write, &want_expected, 1, memory_order_relaxed, memory_order_relaxed)) {
        if (conn_epoll_mod(c, epfd, 1) < 0) {
          atomic_store_explicit(&c->want_write, 0, memory_order_relaxed);
          return CONN_FLUSH_ERROR;
        }
      }
      atomic_store_explicit(&c->last_active, conn_now_s(), memory_order_relaxed);
      return CONN_FLUSH_MORE;
    }
    return CONN_FLUSH_ERROR;
  }

  /* drained: disarm EPOLLOUT if armed */
  atomic_store_explicit(&c->last_active, conn_now_s(), memory_order_relaxed);
  pthread_mutex_lock(&c->out_lock);
  if (c->out.len != c->out.off) { /* racing sender saw want_write set, skipped arm */
    pthread_mutex_unlock(&c->out_lock);
    return conn_flush(c, epfd);
  }
  unsigned char want_expected = 1;
  if (atomic_compare_exchange_strong_explicit(&c->want_write, &want_expected, 0, memory_order_relaxed, memory_order_relaxed)) {
    if (conn_epoll_mod(c, epfd, 0) < 0) {
      atomic_store_explicit(&c->want_write, 1, memory_order_relaxed);
      pthread_mutex_unlock(&c->out_lock);
      return CONN_FLUSH_ERROR;
    }
  }
  pthread_mutex_unlock(&c->out_lock);
  return CONN_FLUSH_DONE;
}

int conn_in_reserve(conn_t *c, size_t need) {
  buf_compact(&c->in);
  return buf_ensure(&c->in, c->in.len + need);
}
