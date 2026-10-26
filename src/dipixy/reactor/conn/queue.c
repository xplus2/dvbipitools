/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/reactor_tls.h"

int conn_send_buffered3(conn_t *c, const void *a, size_t alen, const void *b, size_t blen, const void *cc, size_t clen) {
  int rc = 0;
  pthread_mutex_lock(&c->out_lock);
  if (c->dead) {
    pthread_mutex_unlock(&c->out_lock);
    return -1;
  }
  size_t pending = conn_out_pending(c);
  if (pending + alen + blen + clen > CONN_OUT_MAX || (alen && conn_queue(c, a, alen) < 0) || (blen && conn_queue(c, b, blen) < 0) || (clen && conn_queue(c, cc, clen) < 0)) {
    c->dead = 1; /* owner closes on next visit */
    rc = -1;
  }
  /* cross-thread EPOLLOUT arm: safe, interrupts owner's epoll_wait */
  if (c->epfd >= 0 && !atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed)) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = (atomic_load_explicit(&c->read_done, memory_order_relaxed) ? 0u : (uint32_t)EPOLLIN) | (uint32_t)EPOLLOUT;
    ev.data.ptr = c;
    epoll_ctl(c->epfd, EPOLL_CTL_MOD, c->fd, &ev);
  }
  pthread_mutex_unlock(&c->out_lock);
  return rc;
}

int conn_send_buffered(conn_t *c, const void *a, size_t alen, const void *b, size_t blen) {
  return conn_send_buffered3(c, a, alen, b, blen, NULL, 0);
}

size_t conn_out_pending(const conn_t *c) {
  return c->out.len - c->out.off + atomic_load_explicit(&c->wire_pending, memory_order_relaxed);
}

int conn_queue(conn_t *c, const void *data, size_t len) {
  if (len == 0) return 0;
  if (c->out.cap - c->out.len < len) buf_compact(&c->out);
  if (buf_ensure(&c->out, c->out.len + len) < 0) return -1;
  memcpy(c->out.buf + c->out.len, data, len);
  c->out.len += len;
  conn_add_bytes_served(len);
  return 0;
}

int conn_queue_zc(conn_t *c, const void *data, size_t len, conn_zc_release_fn release, void *release_arg) {
  if (len == 0) {
    release(release_arg);
    return 0;
  }
  if (c->ssl || c->zc.active || c->zc_await_count >= CONN_ZC_AWAIT_MAX) {
    int rc = conn_queue(c, data, len);
    release(release_arg);
    return rc;
  }
  c->zc.data = data;
  c->zc.len = len;
  c->zc.off = 0;
  c->zc.any_zc = 0;
  c->zc.zc_ids = 0;
  c->zc.release = release;
  c->zc.release_arg = release_arg;
  c->zc.active = 1;
  conn_add_bytes_served(len);
  return 0;
}

/* credit overlap of completed range [lo,hi] (u32, wrapping) to each awaiting chunk */
static void zc_credit_range(conn_zc_await_t *aw, int n, uint32_t lo, uint32_t hi) {
  int64_t len = (int64_t)(hi - lo) + 1;
  for (int i = 0; i < n; i++) {
    conn_zc_await_t *a = &aw[i];
    int64_t start = (int32_t)(lo - a->id_lo); /* relative to chunk start, may be negative */
    int64_t from = start > 0 ? start : 0;
    int64_t to = start + len < (int64_t)a->ids ? start + len : (int64_t)a->ids;
    if (to > from) a->done += (uint32_t)(to - from);
  }
}

static void zc_credit(uint32_t lo, uint32_t hi, void *arg) {
  conn_t *c = arg;
  zc_credit_range(c->zc_await, c->zc_await_count, lo, hi);
}

#define ZC_LINGER_MAX 64
#define ZC_LINGER_TIMEOUT_S 30

typedef struct {
  int fd;
  time_t deadline;
  int n;
  conn_zc_await_t await[CONN_ZC_AWAIT_MAX + 1];
} zc_linger_t;

static _Thread_local zc_linger_t t_zc_linger[ZC_LINGER_MAX];
static _Thread_local int t_zc_linger_n;

static void zc_linger_credit(uint32_t lo, uint32_t hi, void *arg) {
  zc_linger_t *l = arg;
  zc_credit_range(l->await, l->n, lo, hi);
}

static void zc_linger_finish(int idx) {
  zc_linger_t *l = &t_zc_linger[idx];
  for (int i = 0; i < l->n; i++)
    if (l->await[i].done < l->await[i].ids) l->await[i].release(l->await[i].release_arg);
  close(l->fd);
  *l = t_zc_linger[--t_zc_linger_n];
}

int conn_zc_linger(conn_t *c) {
  zc_linger_t *l;
  if (c->ssl || t_zc_linger_n == ZC_LINGER_MAX) return 0;
  conn_zc_drain(c);
  if (!c->zc_await_count && !(c->zc.active && c->zc.any_zc)) return 0;
  l = &t_zc_linger[t_zc_linger_n++];
  l->fd = c->fd;
  l->deadline = conn_now_s() + ZC_LINGER_TIMEOUT_S;
  l->n = c->zc_await_count;
  memcpy(l->await, c->zc_await, (size_t)c->zc_await_count * sizeof l->await[0]);
  c->zc_await_count = 0;
  if (c->zc.active && c->zc.any_zc) {
    conn_zc_await_t *a = &l->await[l->n++];
    a->id_lo = c->zc.first_zc_id;
    a->ids = c->zc.zc_ids;
    a->done = 0;
    a->release = c->zc.release;
    a->release_arg = c->zc.release_arg;
    c->zc.active = 0;
  }
  shutdown(l->fd, SHUT_WR);
  return 1;
}

void conn_zc_linger_poll(void) {
  time_t now;
  if (!t_zc_linger_n) return;
  now = conn_now_s();
  for (int i = 0; i < t_zc_linger_n;) {
    zc_linger_t *l = &t_zc_linger[i];
    int pending = 0;
    tls_zc_drain(l->fd, zc_linger_credit, l);
    for (int k = 0; k < l->n; k++)
      if (l->await[k].done < l->await[k].ids) pending = 1;
    if (!pending || now >= l->deadline) zc_linger_finish(i);
    else i++;
  }
}

void conn_zc_linger_flush(void) {
  while (t_zc_linger_n) {
    zc_linger_t *l = &t_zc_linger[t_zc_linger_n - 1];
    tls_zc_drain(l->fd, zc_linger_credit, l);
    zc_linger_finish(t_zc_linger_n - 1);
  }
}

void conn_zc_drain(conn_t *c) {
  if (!tls_zc_drain(c->fd, zc_credit, c)) return;
  int w = 0;
  for (int i = 0; i < c->zc_await_count; i++) {
    if (c->zc_await[i].done >= c->zc_await[i].ids) c->zc_await[i].release(c->zc_await[i].release_arg);
    else c->zc_await[w++] = c->zc_await[i];
  }
  c->zc_await_count = w;
}
