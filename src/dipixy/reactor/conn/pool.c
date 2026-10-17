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

static atomic_long g_conn_active = 0;

typedef struct {
  _Alignas(64) atomic_ullong bytes;
} bytes_slot_t;

static bytes_slot_t g_bytes_served[REACTOR_MAX_WORKERS];
static bytes_slot_t g_bytes_served_fallback;

long reactor_connections_active(void) { return atomic_load_explicit(&g_conn_active, memory_order_relaxed); }

unsigned long long reactor_bytes_served_total(void) {
  unsigned long long total = atomic_load_explicit(&g_bytes_served_fallback.bytes, memory_order_relaxed);
  for (int i = 0; i < REACTOR_MAX_WORKERS; i++) total += atomic_load_explicit(&g_bytes_served[i].bytes, memory_order_relaxed);
  return total;
}

void conn_add_bytes_served(size_t len) {
  int tid = t_reactor_tid;
  bytes_slot_t *slot = (tid >= 0 && tid < REACTOR_MAX_WORKERS) ? &g_bytes_served[tid] : &g_bytes_served_fallback;
  atomic_fetch_add_explicit(&slot->bytes, len, memory_order_relaxed);
}

/* preallocated at conn_new: covers header-only requests/responses with no realloc */
#define CONN_BUF_INITIAL 4096

/* pooled conns keep buffers up to this, larger freed on reclaim */
#define CONN_BUF_KEEP_MAX (64u * 1024)

/* doubling growth. 0 ok, -1 OOM */
int buf_ensure(conn_buf *b, size_t need) {
  if (b->cap >= need) return 0;
  size_t ncap = b->cap ? b->cap : 256;
  while (ncap < need) {
    if (ncap > (SIZE_MAX >> 1)) { /* overflow guard: clamp to exact need */
      ncap = need;
      break;
    }
    ncap <<= 1;
  }
  unsigned char *nb = realloc(b->buf, ncap);
  if (!nb) return -1;
  b->buf = nb;
  b->cap = ncap;
  return 0;
}

/* drop [0,off) prefix: reuse buffer, avoid unbounded growth */
void buf_compact(conn_buf *b) {
  if (b->off == 0) return;
  size_t rem = b->len - b->off;
  if (rem) memmove(b->buf, b->buf + b->off, rem);
  b->len = rem;
  b->off = 0;
}

/* data.ptr = conn: reactor's convention for identifying events */
time_t conn_now_s(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec;
}

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

typedef struct {
  conn_t *c;
  uint64_t mark[QSBR_MAX_WORKERS];
  uint64_t pump_mark[CONN_PUMP_MARKS];
} conn_retiree_t;

static void (*g_pump_mark)(uint64_t *);
static int (*g_pump_passed)(const uint64_t *);

void conn_set_pump_qsbr(void (*mark)(uint64_t *), int (*passed)(const uint64_t *)) {
  g_pump_mark = mark;
  g_pump_passed = passed;
}

static _Thread_local conn_retiree_t t_conn_retiring[CONN_POOL_MAX];
static _Thread_local int t_conn_retiring_n;

static void buf_release(conn_buf *b) {
  free(b->buf);
  memset(b, 0, sizeof *b);
}

static void buf_recycle(conn_buf *b) {
  if (b->cap > CONN_BUF_KEEP_MAX) buf_release(b);
  else {
    b->len = 0;
    b->off = 0;
  }
}

static void conn_pool_reclaim(void) {
  for (int i = 0; i < t_conn_retiring_n;) {
    if (qsbr_mark_passed(reactor_qsbr(), t_conn_retiring[i].mark) && (!g_pump_passed || g_pump_passed(t_conn_retiring[i].pump_mark))) {
      conn_t *c = t_conn_retiring[i].c;
      t_conn_retiring[i] = t_conn_retiring[--t_conn_retiring_n];
      if (t_conn_pool_n < CONN_POOL_MAX) {
        buf_recycle(&c->in);
        buf_recycle(&c->out);
        buf_recycle(&c->wire);
        t_conn_pool[t_conn_pool_n++] = c;
      } else {
        free(c->in.buf);
        free(c->out.buf);
        free(c->wire.buf);
        pthread_mutex_destroy(&c->out_lock);
        free(c);
      }
    } else {
      i++;
    }
  }
}

static conn_t **g_grave;
static size_t g_grave_n;
static size_t g_grave_cap;
static pthread_mutex_t g_grave_mtx = PTHREAD_MUTEX_INITIALIZER;

static void conn_grave_add(conn_t *c) {
  if (g_grave_n == g_grave_cap) {
    size_t ncap = g_grave_cap ? g_grave_cap * 2 : 64;
    conn_t **p = realloc(g_grave, ncap * sizeof *p);
    if (!p) return;
    g_grave = p;
    g_grave_cap = ncap;
  }
  g_grave[g_grave_n++] = c;
}

void conn_retire_handoff(void) {
  pthread_mutex_lock(&g_grave_mtx);
  for (int i = 0; i < t_conn_pool_n; i++) conn_grave_add(t_conn_pool[i]);
  for (int i = 0; i < t_conn_retiring_n; i++) conn_grave_add(t_conn_retiring[i].c);
  pthread_mutex_unlock(&g_grave_mtx);
  t_conn_pool_n = 0;
  t_conn_retiring_n = 0;
}

void conn_graveyard_free(void) {
  pthread_mutex_lock(&g_grave_mtx);
  for (size_t i = 0; i < g_grave_n; i++) {
    conn_t *c = g_grave[i];
    free(c->in.buf);
    free(c->out.buf);
    free(c->wire.buf);
    pthread_mutex_destroy(&c->out_lock);
    free(c);
  }
  free(g_grave);
  g_grave = NULL;
  g_grave_n = 0;
  g_grave_cap = 0;
  pthread_mutex_unlock(&g_grave_mtx);
}

static _Atomic uint64_t g_conn_gen;

conn_t *conn_new(int fd, void *ssl) {
  conn_t *c;
  conn_buf in = {0};
  conn_buf out = {0};
  conn_buf wire = {0};
  conn_pool_reclaim();
  if (t_conn_pool_n > 0) {
    c = t_conn_pool[--t_conn_pool_n];
    in = c->in;
    out = c->out;
    wire = c->wire;
  } else {
    c = malloc(sizeof *c);
    if (!c) return NULL;
    pthread_mutex_init(&c->out_lock, NULL);
  }
  memset(c, 0, offsetof(conn_t, out_lock));
  memset((char *)c + offsetof(conn_t, out_lock) + sizeof c->out_lock, 0, sizeof *c - offsetof(conn_t, out_lock) - sizeof c->out_lock);
  c->in = in;
  c->out = out;
  c->wire = wire;
  c->fd = fd;
  atomic_store_explicit(&c->gen, atomic_fetch_add_explicit(&g_conn_gen, 1, memory_order_relaxed) + 1, memory_order_relaxed);
  c->ssl = ssl;
  c->state = ssl ? CONN_TLS_HANDSHAKE : CONN_READING;
  c->slot = 0;
  c->epfd = -1;
  c->reactor_tid = -1;
  atomic_store_explicit(&c->last_active, conn_now_s(), memory_order_relaxed);
  if (buf_ensure(&c->in, CONN_BUF_INITIAL) < 0 || buf_ensure(&c->out, CONN_BUF_INITIAL) < 0) {
    free(c->in.buf);
    free(c->out.buf);
    free(c->wire.buf);
    pthread_mutex_destroy(&c->out_lock);
    free(c);
    return NULL;
  }
  atomic_fetch_add_explicit(&g_conn_active, 1, memory_order_relaxed);
  return c;
}

void conn_free(conn_t *c) {
  if (!c) return;
  if (c->zc.active)
    c->zc.release(c->zc.release_arg);
  for (int i = 0; i < c->zc_await_count; i++)
    c->zc_await[i].release(c->zc_await[i].release_arg);
  atomic_fetch_sub_explicit(&g_conn_active, 1, memory_order_relaxed);
  conn_pool_reclaim();
  if (t_conn_retiring_n < CONN_POOL_MAX) {
    qsbr_mark(reactor_qsbr(), t_conn_retiring[t_conn_retiring_n].mark);
    if (g_pump_mark) g_pump_mark(t_conn_retiring[t_conn_retiring_n].pump_mark);
    t_conn_retiring[t_conn_retiring_n].c = c;
    t_conn_retiring_n++;
  } else {
    log_code("YW500208", TOOL_NAME ": conn retire queue full, leaking one conn_t");
  }
}
