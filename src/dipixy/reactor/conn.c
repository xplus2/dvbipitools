/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "conn.h"
#include "reactor.h"
#include "reactor_tls.h"
#include "../version.h"
#include "lib/helper/log.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

static atomic_long g_conn_active = 0;
static atomic_ullong g_bytes_served_total = 0;

long reactor_connections_active(void) { return atomic_load_explicit(&g_conn_active, memory_order_relaxed); }

unsigned long long reactor_bytes_served_total(void) { return atomic_load_explicit(&g_bytes_served_total, memory_order_relaxed); }

/* Unsent backlog cap: past this, drop conn (stuck consumer). */
#define CONN_OUT_MAX (4u * 1024 * 1024)

/* preallocated at conn_new: covers header-only requests/responses with no realloc */
#define CONN_BUF_INITIAL 4096

/* doubling growth. 0 ok, -1 OOM */
static int buf_ensure(conn_buf *b, size_t need) {
  if (b->cap >= need)
    return 0;
  size_t ncap = b->cap ? b->cap : 256;
  while (ncap < need) {
    if (ncap > (SIZE_MAX >> 1)) { /* overflow guard: clamp to exact need */
      ncap = need;
      break;
    }
    ncap <<= 1;
  }
  unsigned char *nb = realloc(b->buf, ncap);
  if (!nb)
    return -1;
  b->buf = nb;
  b->cap = ncap;
  return 0;
}

/* drop [0,off) prefix: reuse buffer, avoid unbounded growth */
static void buf_compact(conn_buf *b) {
  if (b->off == 0)
    return;
  size_t rem = b->len - b->off;
  if (rem)
    memmove(b->buf, b->buf + b->off, rem);
  b->len = rem;
  b->off = 0;
}

/* data.ptr = conn: reactor's convention for identifying events */
int conn_epoll_mod(conn_t *c, int epfd, int want_out) {
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = (atomic_load_explicit(&c->read_done, memory_order_relaxed) ? 0u : (uint32_t)EPOLLIN) | (want_out ? (uint32_t)EPOLLOUT : 0u);
  ev.data.ptr = c;
  return epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

#define CONN_POOL_MAX 512

static _Thread_local conn_t *t_conn_pool[CONN_POOL_MAX];
static _Thread_local int t_conn_pool_n;

conn_t *conn_new(int fd, void *ssl) {
  conn_t *c;
  if (t_conn_pool_n > 0) {
    c = t_conn_pool[--t_conn_pool_n];
  } else {
    c = malloc(sizeof *c);
    if (!c)
      return NULL;
    pthread_mutex_init(&c->out_lock, NULL);
  }
  {
    /* out_lock stays live across reuse: never destroyed/reinitialized */
    pthread_mutex_t lock = c->out_lock;
    memset(c, 0, sizeof *c);
    c->out_lock = lock;
  }
  c->fd = fd;
  c->ssl = ssl;
  c->state = ssl ? CONN_TLS_HANDSHAKE : CONN_READING;
  c->slot = 0;
  c->epfd = -1;
  c->reactor_tid = -1;
  c->last_active = time(NULL);
  c->zc_confirmed_id = -1;
  if (buf_ensure(&c->in, CONN_BUF_INITIAL) < 0 || buf_ensure(&c->out, CONN_BUF_INITIAL) < 0) {
    free(c->in.buf);
    free(c->out.buf);
    pthread_mutex_destroy(&c->out_lock);
    free(c);
    return NULL;
  }
  atomic_fetch_add_explicit(&g_conn_active, 1, memory_order_relaxed);
  return c;
}

void conn_free(conn_t *c) {
  if (!c)
    return;
  /* fd closed, pages unpinned: release safe */
  if (c->zc.active)
    c->zc.release(c->zc.release_arg);
  for (int i = 0; i < c->zc_await_count; i++)
    c->zc_await[i].release(c->zc_await[i].release_arg);
  atomic_fetch_sub_explicit(&g_conn_active, 1, memory_order_relaxed);
  free(c->in.buf);
  free(c->out.buf);
  if (t_conn_pool_n < CONN_POOL_MAX) {
    t_conn_pool[t_conn_pool_n++] = c;
  } else {
    pthread_mutex_destroy(&c->out_lock);
    free(c);
  }
}

/* fd->conn table: atomic ptr slots, owner reactor writes, others read.
   racer: old or new value, recheck under out_lock. TLS GC delays fd reuse past close window: no stale hit */
static conn_t **g_fd_conn;
static int g_fd_conn_max = 0;
static atomic_int g_fd_conn_overflow_logged = 0;

#define CONN_TABLE_HEADROOM 64 /* non-stream conns sharing this table: status polls, /ui/ws/, DLNA control */
#define CONN_TABLE_FLOOR 64

int conn_table_capacity(int max_clients) {
  int n = (max_clients > 0 ? max_clients : 0) + CONN_TABLE_HEADROOM;
  if (n < CONN_TABLE_FLOOR)
    n = CONN_TABLE_FLOOR;
  if (n > CONN_TABLE_MAX)
    n = CONN_TABLE_MAX;
  return n;
}

int conn_table_init(int maxfd) {
  if (g_fd_conn_max || maxfd <= 0)
    return 0;
  if (maxfd > CONN_TABLE_MAX)
    maxfd = CONN_TABLE_MAX;
  g_fd_conn = calloc((size_t)maxfd, sizeof *g_fd_conn);
  if (!g_fd_conn) {
    log_line(TOOL_NAME ": out of memory sizing conn table (%d entries), fd tracking disabled", maxfd);
    return 0;
  }
  g_fd_conn_max = maxfd;
  return 0;
}

/* shouldn't happen: table sized off max_clients + headroom. logged once, not per fd */
static void conn_table_overflow_log(int fd) {
  if (!atomic_exchange_explicit(&g_fd_conn_overflow_logged, 1, memory_order_relaxed))
    log_line(TOOL_NAME ": conn table overflow, fd %d exceeds table size %d, conn_publish/conn_for_fd no-op past this point", fd, g_fd_conn_max);
}

void conn_publish(conn_t *c) {
  if (!g_fd_conn_max || c->fd < 0)
    return;
  if (c->fd >= g_fd_conn_max) {
    conn_table_overflow_log(c->fd);
    return;
  }
  __atomic_store_n(&g_fd_conn[c->fd], c, __ATOMIC_RELEASE);
}

void conn_unpublish(const conn_t *c) {
  if (g_fd_conn_max && c->fd >= 0 && c->fd < g_fd_conn_max)
    __atomic_store_n(&g_fd_conn[c->fd], (conn_t *)NULL, __ATOMIC_RELEASE);
}

conn_t *conn_for_fd(int fd) {
  if (!g_fd_conn_max || fd < 0)
    return NULL;
  if (fd >= g_fd_conn_max) {
    conn_table_overflow_log(fd);
    return NULL;
  }
  return __atomic_load_n(&g_fd_conn[fd], __ATOMIC_ACQUIRE);
}

void conn_request_close(conn_t *c) {
  pthread_mutex_lock(&c->out_lock);
  c->close_after_flush = 1;
  c->requested_close = 1;
  if (c->epfd >= 0 && !atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed)) { /* wake owner: flush, tear down */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = (atomic_load_explicit(&c->read_done, memory_order_relaxed) ? 0u : (uint32_t)EPOLLIN) | (uint32_t)EPOLLOUT;
    ev.data.ptr = c;
    epoll_ctl(c->epfd, EPOLL_CTL_MOD, c->fd, &ev);
  }
  pthread_mutex_unlock(&c->out_lock);
}

void conn_sweep_idle(unsigned idle_timeout_s) {
  time_t now;
  if (!idle_timeout_s || !g_fd_conn_max) return;
  now = time(NULL);
  for (int fd = 0; fd < g_fd_conn_max; fd++) {
    conn_t *c = __atomic_load_n(&g_fd_conn[fd], __ATOMIC_ACQUIRE);
    if (c && (unsigned)(now - c->last_active) > idle_timeout_s)
      conn_request_close(c);
  }
}

int conn_send_buffered(conn_t *c, const void *a, size_t alen, const void *b, size_t blen) {
  int rc = 0;
  pthread_mutex_lock(&c->out_lock);
  if (c->dead) {
    pthread_mutex_unlock(&c->out_lock);
    return -1;
  }
  size_t pending = c->out.len - c->out.off;
  if (pending + alen + blen > CONN_OUT_MAX) {
    c->dead = 1; /* slow consumer: owner closes on next visit */
    rc = -1;
  } else if ((alen && conn_queue(c, a, alen) < 0) || (blen && conn_queue(c, b, blen) < 0)) {
    c->dead = 1; /* OOM */
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

int conn_queue(conn_t *c, const void *data, size_t len) {
  if (len == 0)
    return 0;
  buf_compact(&c->out);
  if (buf_ensure(&c->out, c->out.len + len) < 0)
    return -1;
  memcpy(c->out.buf + c->out.len, data, len);
  c->out.len += len;
  atomic_fetch_add_explicit(&g_bytes_served_total, len, memory_order_relaxed);
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
  c->zc.last_zc_id = -1;
  c->zc.release = release;
  c->zc.release_arg = release_arg;
  c->zc.active = 1;
  atomic_fetch_add_explicit(&g_bytes_served_total, len, memory_order_relaxed);
  return 0;
}

void conn_zc_drain(conn_t *c) {
  int hi = -1;
  if (!tls_zc_drain(c->fd, &hi))
    return;
  if (hi > c->zc_confirmed_id)
    c->zc_confirmed_id = hi;
  int w = 0;
  for (int i = 0; i < c->zc_await_count; i++) {
    if (c->zc_await[i].zc_id_hi <= c->zc_confirmed_id)
      c->zc_await[i].release(c->zc_await[i].release_arg);
    else
      c->zc_await[w++] = c->zc_await[i];
  }
  c->zc_await_count = w;
}

int conn_flush(conn_t *c, int epfd) {
  conn_zc_drain(c);

  while (c->out.off < c->out.len) {
    ssize_t n =
        tls_net_send(c->fd, c->out.buf + c->out.off, c->out.len - c->out.off);
    if (n > 0) {
      c->out.off += (size_t)n;
      continue;
    }
    /* tls_net_send maps SSL_WANT_READ/WRITE to EAGAIN. EPOLLIN stays armed: covers WANT_READ mid-renegotiation.
       EPOLLOUT added: full send buffer also wakes */
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      unsigned char want_expected = 0;
      if (atomic_compare_exchange_strong_explicit(&c->want_write, &want_expected, 1, memory_order_relaxed, memory_order_relaxed)) {
        if (conn_epoll_mod(c, epfd, 1) < 0) {
          atomic_store_explicit(&c->want_write, 0, memory_order_relaxed);
          return CONN_FLUSH_ERROR;
        }
      }
      c->last_active = time(NULL);
      return CONN_FLUSH_MORE;
    }
    return CONN_FLUSH_ERROR;
  }

  while (c->zc.active) {
    int used_zc = 0;
    ssize_t n = tls_net_send_zc(c->fd, c->zc.data + c->zc.off, c->zc.len - c->zc.off, &used_zc);
    if (n > 0) {
      c->zc.off += (size_t)n;
      if (used_zc) {
        c->zc.any_zc = 1;
        c->zc.last_zc_id = c->zc_next_id++;
      }
      if (c->zc.off < c->zc.len)
        continue;
      /* fully handed off: release now if never pinned, else await confirm */
      if (!c->zc.any_zc)
        c->zc.release(c->zc.release_arg);
      else {
        c->zc_await[c->zc_await_count].zc_id_hi = c->zc.last_zc_id;
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
      c->last_active = time(NULL);
      return CONN_FLUSH_MORE;
    }
    return CONN_FLUSH_ERROR;
  }

  /* drained: reset, disarm EPOLLOUT if armed */
  c->out.off = c->out.len = 0;
  c->last_active = time(NULL);
  unsigned char want_expected = 1;
  if (atomic_compare_exchange_strong_explicit(&c->want_write, &want_expected, 0, memory_order_relaxed, memory_order_relaxed)) {
    if (conn_epoll_mod(c, epfd, 0) < 0) {
      atomic_store_explicit(&c->want_write, 1, memory_order_relaxed);
      return CONN_FLUSH_ERROR;
    }
  }
  return CONN_FLUSH_DONE;
}

int conn_in_reserve(conn_t *c, size_t need) {
  buf_compact(&c->in);
  return buf_ensure(&c->in, c->in.len + need);
}

