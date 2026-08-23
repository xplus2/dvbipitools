/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/log.h"

#include "priv.h"

/* CLOCK_MONOTONIC: immune to wall-clock jumps */
static void log_throttled(log_throttle_t *t, const char *msg) {
  struct timespec now;
  long long now_ms, next, new_next;

  clock_gettime(CLOCK_MONOTONIC, &now);
  now_ms = (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
  next = atomic_load_explicit(&t->next_log_ms, memory_order_relaxed);
  if (now_ms >= next) {
    new_next = now_ms + EMMG_DROP_LOG_WINDOW_S * 1000;
    if (atomic_compare_exchange_strong_explicit(&t->next_log_ms, &next, new_next, memory_order_relaxed, memory_order_relaxed)) {
      unsigned long suppressed = atomic_exchange_explicit(&t->suppressed, 0, memory_order_relaxed);
      if (suppressed)
        log_line("emmg: %s (%lu more in last %ds)", msg, suppressed, EMMG_DROP_LOG_WINDOW_S);
      else
        log_line("emmg: %s", msg);
      return;
    }
  }
  atomic_fetch_add_explicit(&t->suppressed, 1, memory_order_relaxed);
}

void publish_datagram_cb(const unsigned char *data, unsigned short len, void *user) {
  emmg_server_t *s = user;
  if (len > EMMG_MAX_DATAGRAM_LEN) {
    log_throttled(&s->oversized_throttle, "dropping oversized EMM datagram");
    atomic_fetch_add_explicit(&s->emm_dropped, 1, memory_order_relaxed);
    return;
  }
  pthread_mutex_lock(&s->queue_lock);
  if (atomic_load_explicit(&s->queue_len, memory_order_relaxed) == EMMG_QUEUE_CAP) {
    s->queue_head = (s->queue_head + 1) % EMMG_QUEUE_CAP;
    atomic_fetch_sub_explicit(&s->queue_len, 1, memory_order_relaxed);
    log_throttled(&s->queue_full_throttle, "EMM queue full, dropping oldest datagram");
    atomic_fetch_add_explicit(&s->emm_dropped, 1, memory_order_relaxed);
  }
  {
    size_t idx = (s->queue_head + atomic_load_explicit(&s->queue_len, memory_order_relaxed)) % EMMG_QUEUE_CAP;
    memcpy(s->queue[idx].data, data, len);
    s->queue[idx].len = len;
    atomic_fetch_add_explicit(&s->queue_len, 1, memory_order_relaxed);
  }
  pthread_mutex_unlock(&s->queue_lock);
  atomic_fetch_add_explicit(&s->emm_total, 1, memory_order_relaxed);
}

int emmg_server_dequeue_emm(emmg_server_t *s, unsigned char *out, size_t cap, size_t *len_out) {
  int have;

  /* called every packet, almost always empty: skip lock on miss. */
  if (!atomic_load_explicit(&s->queue_len, memory_order_relaxed))
    return -1;

  pthread_mutex_lock(&s->queue_lock);
  have = atomic_load_explicit(&s->queue_len, memory_order_relaxed) > 0;
  if (have) {
    size_t len = s->queue[s->queue_head].len;
    if (cap < len) {
      pthread_mutex_unlock(&s->queue_lock);
      return -1;
    }
    memcpy(out, s->queue[s->queue_head].data, len);
    *len_out = len;
    s->queue_head = (s->queue_head + 1) % EMMG_QUEUE_CAP;
    atomic_fetch_sub_explicit(&s->queue_len, 1, memory_order_relaxed);
  }
  pthread_mutex_unlock(&s->queue_lock);
  return have ? 0 : -1;
}

unsigned long emmg_server_emm_dropped_total(emmg_server_t *s) { return atomic_load_explicit(&s->emm_dropped, memory_order_relaxed); }

unsigned emmg_server_client_count(emmg_server_t *s) {
  unsigned i, n = 0;
  for (i = 0; i < s->max_conns; i++)
    if (atomic_load_explicit(&s->worker_active[i], memory_order_relaxed))
      n++;
  return n;
}

unsigned long emmg_server_emm_total(emmg_server_t *s) { return atomic_load_explicit(&s->emm_total, memory_order_relaxed); }

static int tcp_listen_dualstack(unsigned port) {
  struct sockaddr_in6 addr;
  int fd, on = 1, off = 0, flags;

  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd < 0) {
    log_line("emmg: socket: %s", strerror(errno));
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);

  memset(&addr, 0, sizeof addr);
  addr.sin6_family = AF_INET6;
  addr.sin6_addr = in6addr_any;
  addr.sin6_port = htons((unsigned short)port);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    log_line("emmg: bind :%u: %s", port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 8) < 0) {
    log_line("emmg: listen: %s", strerror(errno));
    close(fd);
    return -1;
  }
  flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    log_line("emmg: fcntl O_NONBLOCK: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

emmg_server_t *emmg_server_start(const emmg_server_cfg_t *cfg) {
  emmg_server_t *s = calloc(1, sizeof *s);
  if (!s)
    return NULL;

  s->max_conns = cfg->max_conns ? cfg->max_conns : 8;
  if (s->max_conns > EMMG_MAX_CONNS_CEILING)
    s->max_conns = EMMG_MAX_CONNS_CEILING;

  s->listen_fd = tcp_listen_dualstack(cfg->port);
  if (s->listen_fd < 0) {
    free(s);
    return NULL;
  }
  pthread_mutex_init(&s->queue_lock, NULL);

  if (pthread_create(&s->accept_thread, NULL, accept_main, s) != 0) {
    log_line("emmg: pthread_create: %s", strerror(errno));
    close(s->listen_fd);
    pthread_mutex_destroy(&s->queue_lock);
    free(s);
    return NULL;
  }
  return s;
}

/* useful when cfg.port was 0 (kernel-assigned ephemeral port) */
unsigned emmg_server_port(emmg_server_t *s) {
  struct sockaddr_in6 addr;
  socklen_t alen = sizeof addr;
  if (getsockname(s->listen_fd, (struct sockaddr *)&addr, &alen) < 0)
    return 0;
  return ntohs(addr.sin6_port);
}

void emmg_server_stop(emmg_server_t *s) {
  if (!s)
    return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  close(s->listen_fd);

  for (unsigned i = 0; i < s->max_conns; i++)
    if (s->worker_thread_joinable[i]) {
      pthread_join(s->worker_thread[i], NULL);
      s->worker_thread_joinable[i] = 0;
    }
  pthread_mutex_destroy(&s->queue_lock);
  free(s);
}
