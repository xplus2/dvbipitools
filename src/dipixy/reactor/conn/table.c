/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

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

#include "dipixy/reactor/conn.h"
#include "dipixy/version.h"
#include "lib/helper/log.h"

/* fd->conn table: atomic ptr slots, owner reactor writes, others read.
   racer: old or new value, recheck under out_lock. TLS GC delays fd reuse past close window: no stale hit */
static conn_t **g_fd_conn;
static int g_fd_conn_max = 0;
static atomic_int g_fd_conn_overflow_logged = 0;

#define CONN_TABLE_HEADROOM 64 /* non-stream conns sharing this table: status polls, /ui/ws/, DLNA control */
#define CONN_TABLE_FLOOR 64

int conn_table_capacity(int max_clients) {
  struct rlimit rl;
  int n = (max_clients > 0 ? max_clients : 0) + CONN_TABLE_HEADROOM;
  if (getrlimit(RLIMIT_NOFILE, &rl) == 0) { /* idle conns bypass max_clients: any fd below limit must fit */
    long lim = rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur > CONN_TABLE_MAX ? CONN_TABLE_MAX : (long)rl.rlim_cur;
    if (lim > n) n = (int)lim;
  }
  if (n < CONN_TABLE_FLOOR) n = CONN_TABLE_FLOOR;
  if (n > CONN_TABLE_MAX) n = CONN_TABLE_MAX;
  return n;
}

int conn_table_init(int maxfd) {
  if (g_fd_conn_max || maxfd <= 0) return 0;
  if (maxfd > CONN_TABLE_MAX) maxfd = CONN_TABLE_MAX;
  g_fd_conn = calloc((size_t)maxfd, sizeof *g_fd_conn);
  if (!g_fd_conn) {
    log_code("YA500209", TOOL_NAME ": out of memory sizing conn table (%d entries), fd tracking disabled", maxfd);
    return 0;
  }
  g_fd_conn_max = maxfd;
  return 0;
}

/* only if RLIMIT_NOFILE exceeds CONN_TABLE_MAX. accept path refuses such fds. logged once */
static void conn_table_overflow_log(int fd) {
  if (!atomic_exchange_explicit(&g_fd_conn_overflow_logged, 1, memory_order_relaxed))
    log_code("YE500210", TOOL_NAME ": conn table overflow, fd %d exceeds table size %d, conn_publish/conn_for_fd no-op past this point", fd, g_fd_conn_max);
}

int conn_fd_trackable(int fd) {
  return g_fd_conn_max && fd >= 0 && fd < g_fd_conn_max;
}

void conn_publish(conn_t *c) {
  if (!g_fd_conn_max || c->fd < 0) return;
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
  if (!g_fd_conn_max || fd < 0) return NULL;
  if (fd >= g_fd_conn_max) {
    conn_table_overflow_log(fd);
    return NULL;
  }
  return __atomic_load_n(&g_fd_conn[fd], __ATOMIC_ACQUIRE);
}

conn_t *conn_for_fd_gen(int fd, uint64_t gen) {
  conn_t *c = conn_for_fd(fd);
  if (!c || !gen || atomic_load_explicit(&c->gen, memory_order_relaxed) != gen) return NULL;
  return c;
}

uint64_t conn_gen_for_fd(int fd) {
  const conn_t *c = conn_for_fd(fd);
  return c ? atomic_load_explicit(&c->gen, memory_order_relaxed) : 0;
}

void conn_request_close(conn_t *c) {
  pthread_mutex_lock(&c->out_lock);
  atomic_store_explicit(&c->close_requested, 1, memory_order_release);
  if (!c->torn_down && c->epfd >= 0 && !atomic_exchange_explicit(&c->want_write, 1, memory_order_relaxed)) { /* wake owner: flush, tear down */
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = (atomic_load_explicit(&c->read_done, memory_order_relaxed) ? 0u : (uint32_t)EPOLLIN) | (uint32_t)EPOLLOUT;
    ev.data.ptr = c;
    epoll_ctl(c->epfd, EPOLL_CTL_MOD, c->fd, &ev);
  }
  pthread_mutex_unlock(&c->out_lock);
}

/* request close, then force EPOLLIN: EPOLLOUT alone never fires if send buffer is full + armed */
static void conn_idle_close(conn_t *c) {
  conn_request_close(c);
  pthread_mutex_lock(&c->out_lock);
  if (!c->torn_down) shutdown(c->fd, SHUT_RD);
  pthread_mutex_unlock(&c->out_lock);
}

void conn_sweep_idle(unsigned idle_timeout_s) {
  static _Atomic time_t last_sweep = 0;
  time_t now;
  time_t prev;
  if (!idle_timeout_s || !g_fd_conn_max) return;
  now = conn_now_s();
  prev = atomic_load_explicit(&last_sweep, memory_order_relaxed);
  if (now == prev) return; /* called on every epoll wake, cap scan rate to 1/s */
  if (!atomic_compare_exchange_strong_explicit(&last_sweep, &prev, now, memory_order_relaxed, memory_order_relaxed)) return; /* another thread won */
  for (int fd = 0; fd < g_fd_conn_max; fd++) {
    conn_t *c = __atomic_load_n(&g_fd_conn[fd], __ATOMIC_ACQUIRE);
    if (c && (unsigned)(now - atomic_load_explicit(&c->last_active, memory_order_relaxed)) > idle_timeout_s) conn_idle_close(c);
  }
}
