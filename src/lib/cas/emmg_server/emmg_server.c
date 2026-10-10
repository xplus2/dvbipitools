/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/log.h"

#include "priv.h"

static int is_one_section(const unsigned char *data, unsigned short len) {
  return len >= EMMG_SECTION_HDR_LEN && len == EMMG_SECTION_HDR_LEN + (((data[1] & 0x0F) << 8) | data[2]);
}

static double mono_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void publish_datagram_cb(const unsigned char *data, unsigned short len, void *user) {
  emmg_server_t *s = user;
  size_t idx;
  if (len > EMMG_MAX_DATAGRAM_LEN) {
    log_throttled(&s->oversized_throttle, LOG_THROTTLE_WINDOW_S, "emmg: dropping oversized EMM datagram");
    atomic_fetch_add_explicit(&s->emm_dropped, 1, memory_order_relaxed);
    return;
  }
  if (!is_one_section(data, len)) {
    log_throttled(&s->malformed_throttle, LOG_THROTTLE_WINDOW_S, "emmg: dropping datagram that is not a well-formed section");
    atomic_fetch_add_explicit(&s->emm_dropped, 1, memory_order_relaxed);
    return;
  }
  pthread_mutex_lock(&s->queue_lock);
  if (atomic_load_explicit(&s->queue_len, memory_order_relaxed) == EMMG_QUEUE_CAP) {
    s->queue_head = (s->queue_head + 1) % EMMG_QUEUE_CAP;
    atomic_fetch_sub_explicit(&s->queue_len, 1, memory_order_relaxed);
    log_throttled(&s->queue_full_throttle, LOG_THROTTLE_WINDOW_S, "emmg: EMM queue full, dropping oldest datagram");
    atomic_fetch_add_explicit(&s->emm_dropped, 1, memory_order_relaxed);
  }
  idx = (s->queue_head + atomic_load_explicit(&s->queue_len, memory_order_relaxed)) % EMMG_QUEUE_CAP;
  memcpy(s->queue[idx].data, data, len);
  s->queue[idx].len = len;
  atomic_fetch_add_explicit(&s->queue_len, 1, memory_order_relaxed);
  pthread_mutex_unlock(&s->queue_lock);
  atomic_fetch_add_explicit(&s->emm_total, 1, memory_order_relaxed);
}

static void refill_tokens(emmg_server_t *s, double rate) {
  double now = mono_s();
  double burst = rate / 4;
  if (burst < EMMG_MAX_DATAGRAM_LEN) burst = EMMG_MAX_DATAGRAM_LEN;
  s->tokens = s->tokens_ts > 0.0 ? s->tokens + (now - s->tokens_ts) * rate : burst;
  if (s->tokens > burst) s->tokens = burst;
  s->tokens_ts = now;
}

int emmg_server_dequeue_emm(emmg_server_t *s, unsigned char *out, size_t cap, size_t *len_out) {
  int have;
  unsigned kbps = 0;

  /* called every packet, almost always empty: skip lock on miss. */
  if (!atomic_load_explicit(&s->queue_len, memory_order_relaxed)) return -1;

  for (unsigned i = 0; i < EMMG_MAX_CONNS_CEILING; i++) kbps += atomic_load_explicit(&s->granted_kbps[i], memory_order_relaxed);

  if (!kbps) kbps = EMMG_DEFAULT_KBPS;

  pthread_mutex_lock(&s->queue_lock);
  have = atomic_load_explicit(&s->queue_len, memory_order_relaxed) > 0;
  if (have) {
    refill_tokens(s, (double)kbps * EMMG_BYTES_PER_KBPS_S);
    if (s->tokens <= 0.0) have = 0;
  } else {
    s->tokens_ts = 0.0;
  }
  if (have) {
    size_t len = s->queue[s->queue_head].len;
    if (cap < len) {
      pthread_mutex_unlock(&s->queue_lock);
      return -1;
    }
    s->tokens -= (double)len;
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
  unsigned i;
  unsigned n = 0;
  if (s->dial_mode) return atomic_load_explicit(&s->dial_connected, memory_order_relaxed) ? 1 : 0;
  for (i = 0; i < s->max_conns; i++) {
    if (atomic_load_explicit(&s->worker_active[i], memory_order_relaxed)) n++;
  }
  return n;
}

unsigned long emmg_server_emm_total(emmg_server_t *s) { return atomic_load_explicit(&s->emm_total, memory_order_relaxed); }

static int socket_wildcard(unsigned port, struct sockaddr_storage *ss, socklen_t *sslen) {
  int off = 0;
  int fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd >= 0) {
    struct sockaddr_in6 *a6 = (struct sockaddr_in6 *)ss;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off);
    a6->sin6_family = AF_INET6;
    a6->sin6_addr = in6addr_any;
    a6->sin6_port = htons((unsigned short)port);
    *sslen = sizeof *a6;
  } else if (errno == EAFNOSUPPORT) {
    struct sockaddr_in *a4 = (struct sockaddr_in *)ss;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      log_line("emmg: socket: %s", strerror(errno));
      return -1;
    }
    a4->sin_family = AF_INET;
    a4->sin_addr.s_addr = htonl(INADDR_ANY);
    a4->sin_port = htons((unsigned short)port);
    *sslen = sizeof *a4;
  } else {
    log_line("emmg: socket: %s", strerror(errno));
    return -1;
  }
  return fd;
}

static int socket_host(const char *host, unsigned port, struct sockaddr_storage *ss, socklen_t *sslen) {
  struct addrinfo hints;
  struct addrinfo *res;
  char portstr[8];
  int rc;
  int fd;

  memset(&hints, 0, sizeof hints);
  hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV | AI_PASSIVE;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(portstr, sizeof portstr, "%u", port);
  rc = getaddrinfo(host, portstr, &hints, &res);
  if (rc != 0) {
    log_line("emmg: listen address %s: %s", host, gai_strerror(rc));
    return -1;
  }
  fd = socket(res->ai_family, SOCK_STREAM, 0);
  if (fd < 0) {
    log_line("emmg: socket: %s", strerror(errno));
  } else {
    memcpy(ss, res->ai_addr, res->ai_addrlen);
    *sslen = res->ai_addrlen;
  }
  freeaddrinfo(res);
  return fd;
}

static int tcp_listen(const char *host, unsigned port) {
  struct sockaddr_storage ss;
  socklen_t sslen = 0;
  int fd;
  int on = 1;
  int flags;
  int named = host && host[0];

  memset(&ss, 0, sizeof ss);
  fd = named ? socket_host(host, port, &ss, &sslen) : socket_wildcard(port, &ss, &sslen);
  if (fd < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
  if (bind(fd, (struct sockaddr *)&ss, sslen) < 0) {
    log_line("emmg: bind %s:%u: %s", named ? host : "*", port, strerror(errno));
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

  s->required_version = (unsigned char)cfg->required_version;
  s->dial_mode = cfg->dial_host && cfg->dial_host[0];

  if (s->dial_mode) {
    s->listen_fd = -1;
    s->dial_host = cfg->dial_host;
    s->dial_port = cfg->dial_port;
  } else {
    s->max_conns = cfg->max_conns ? cfg->max_conns : 8;
    if (s->max_conns > EMMG_MAX_CONNS_CEILING)
      s->max_conns = EMMG_MAX_CONNS_CEILING;
    s->listen_fd = tcp_listen(cfg->listen_host, cfg->port);
    if (s->listen_fd < 0) {
      free(s);
      return NULL;
    }
  }
  pthread_mutex_init(&s->queue_lock, NULL);
  if (pthread_create(&s->accept_thread, NULL, s->dial_mode ? dial_main : accept_main, s) != 0) {
    log_line("emmg: pthread_create: %s", strerror(errno));
    if (s->listen_fd >= 0)
      close(s->listen_fd);
    pthread_mutex_destroy(&s->queue_lock);
    free(s);
    return NULL;
  }
  return s;
}

/* useful when cfg.port was 0 (kernel-assigned ephemeral port) */
unsigned emmg_server_port(emmg_server_t *s) {
  struct sockaddr_storage ss;
  socklen_t alen = sizeof ss;
  if (getsockname(s->listen_fd, (struct sockaddr *)&ss, &alen) < 0) return 0;
  if (ss.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *)&ss)->sin6_port);
  return ntohs(((struct sockaddr_in *)&ss)->sin_port);
}

void emmg_server_stop(emmg_server_t *s) {
  if (!s) return;
  atomic_store_explicit(&s->stop, 1, memory_order_relaxed);
  pthread_join(s->accept_thread, NULL);
  if (s->listen_fd >= 0) close(s->listen_fd);
  for (unsigned i = 0; i < EMMG_MAX_CONNS_CEILING; i++) {
    if (s->worker_thread_joinable[i]) {
      pthread_join(s->worker_thread[i], NULL);
      s->worker_thread_joinable[i] = 0;
    }
  }
  pthread_mutex_destroy(&s->queue_lock);
  free(s);
}
