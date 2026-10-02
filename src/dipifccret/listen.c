/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/net/netconnect.h"
#include "lib/sys/signal.h"

#include "listen.h"
#include "version.h"

typedef struct {
  int fd;
  int epfd;
  listen_rtcp_cb cb;
  void *user;
  pthread_t thread;
  atomic_int *stop;
  int has_wake;
} worker_t;

struct listen_pool {
  worker_t *workers;
  unsigned count;
  atomic_int stop;
};

/* drains pending datagrams on fd via recv_cb, stops at EAGAIN/EWOULDBLOCK or a real error.
   log_tag identifies caller in error line */
static void drain_socket(int fd, unsigned char *buf, size_t buflen, const char *log_tag,
                          void (*recv_cb)(const unsigned char *data, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *ctx),
                          void *ctx) {
  for (;;) {
    struct sockaddr_storage from;
    socklen_t fromlen = sizeof from;
    ssize_t r = recvfrom(fd, buf, buflen, 0, (struct sockaddr *)&from, &fromlen);
    if (r < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return;
      if (errno == EINTR)
        continue;
      log_line(TOOL_NAME ": %s: %s", log_tag, strerror(errno));
      return;
    }
    if (recv_cb)
      recv_cb(buf, (size_t)r, fd, (struct sockaddr *)&from, fromlen, ctx);
  }
}

static void worker_recv_cb(const unsigned char *data, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *ctx) {
  worker_t *w = ctx;
  if (w->cb)
    w->cb(data, len, fd, from, fromlen, w->user);
}

static void *worker_main(void *arg) {
  worker_t *w = (worker_t *)arg;
  unsigned char buf[2048];

  while (!signal_stop_requested() && !atomic_load_explicit(w->stop, memory_order_relaxed)) {
    struct epoll_event evs[2];
    int n = epoll_wait(w->epfd, evs, 2, w->has_wake ? -1 : 100);
    for (int i = 0; i < n; i++) if (evs[i].data.fd == w->fd) drain_socket(w->fd, buf, sizeof buf, "recv", worker_recv_cb, w);
  }
  return NULL;
}

static int bind_dgram(int fd, int family, const char *addr, unsigned port) {
  struct sockaddr_storage ss;
  socklen_t sslen;
  if (netaddr_fill(family, addr, port, &ss, &sslen)) {
    log_line(TOOL_NAME ": bad listen address: %s", addr);
    errno = EINVAL; /* inet_pton() never sets errno, give caller's strerror() a real value */
    return -1;
  }
  return bind(fd, (struct sockaddr *)&ss, sslen);
}

static int open_reuseport_socket(int family, const char *addr, unsigned port) {
  int fd, on = 1;

  fd = socket(family, SOCK_DGRAM, 0);
  if (fd < 0) {
    log_line(TOOL_NAME ": socket: %s", strerror(errno));
    return -1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on) < 0) {
    log_line(TOOL_NAME ": SO_REUSEPORT: %s", strerror(errno));
    close(fd);
    return -1;
  }
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { /* required: drain loop below relies on EAGAIN to stop */
    log_line(TOOL_NAME ": fcntl O_NONBLOCK: %s", strerror(errno));
    close(fd);
    return -1;
  }
  if (bind_dgram(fd, family, addr, port) < 0) {
    log_line(TOOL_NAME ": bind: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

listen_pool_t *listen_pool_start(int family, const char *addr, unsigned port, unsigned workers, listen_rtcp_cb cb, void *user) {
  listen_pool_t *p;
  int fd, epfd, rc;

  if (workers == 0) workers = 1;
  p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->workers = calloc(workers, sizeof *p->workers);
  if (!p->workers) {
    free(p);
    return NULL;
  }
  atomic_init(&p->stop, 0);

  for (unsigned i = 0; i < workers; i++) {
    struct epoll_event ev;
    worker_t *w = &p->workers[i];
    int wfd;
    struct epoll_event wev;
    fd = open_reuseport_socket(family, addr, port);
    if (fd < 0) goto fail;
    epfd = epoll_create1(0);
    if (epfd < 0) {
      log_line(TOOL_NAME ": epoll_create1: %s", strerror(errno));
      close(fd);
      goto fail;
    }
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      log_line(TOOL_NAME ": epoll_ctl: %s", strerror(errno));
      close(epfd);
      close(fd);
      goto fail;
    }
    w->fd = fd;
    w->epfd = epfd;
    w->cb = cb;
    w->user = user;
    w->stop = &p->stop;
    w->has_wake = 0;
    wfd = signal_wake_fd();
    if (wfd >= 0) {
      wev.events = EPOLLIN;
      wev.data.fd = wfd;
      if (epoll_ctl(epfd, EPOLL_CTL_ADD, wfd, &wev) == 0)
        w->has_wake = 1;
      else
        log_line(TOOL_NAME ": epoll_ctl (wake fd): %s, falling back to bounded poll", strerror(errno));
    }
    rc = pthread_create(&w->thread, NULL, worker_main, w);
    if (rc != 0) {
      log_line(TOOL_NAME ": pthread_create: %s", strerror(rc));
      close(epfd);
      close(fd);
      goto fail;
    }
    p->count = i + 1;
  }
  return p;

fail:
  atomic_store_explicit(&p->stop, 1, memory_order_relaxed);
  for (unsigned i = 0; i < p->count; i++)
    pthread_join(p->workers[i].thread, NULL);
  for (unsigned i = 0; i < p->count; i++) {
    close(p->workers[i].epfd);
    close(p->workers[i].fd);
  }
  free(p->workers);
  free(p);
  return NULL;
}

void listen_pool_stop(listen_pool_t *p) {
  if (!p)
    return;
  for (unsigned i = 0; i < p->count; i++)
    pthread_join(p->workers[i].thread, NULL);
  for (unsigned i = 0; i < p->count; i++) {
    close(p->workers[i].epfd);
    close(p->workers[i].fd);
  }
  free(p->workers);
  free(p);
}

struct listen_multi {
  int *fds;
  size_t count;
  int epfd;
  pthread_t thread;
  listen_multi_cb cb;
  void *user;
  int has_wake;
};

#define LISTEN_MULTI_WAKE_SLOT ((uint64_t)-1)

typedef struct {
  listen_multi_t *p;
  size_t slot;
} multi_drain_ctx_t;

static void multi_recv_cb(const unsigned char *data, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *ctx) {
  multi_drain_ctx_t *c = ctx;
  if (c->p->cb)
    c->p->cb(data, len, c->slot, fd, from, fromlen, c->p->user);
}

static void *multi_worker_main(void *arg) {
  listen_multi_t *p = (listen_multi_t *)arg;
  unsigned char buf[2048];
  struct epoll_event events[32];

  while (!signal_stop_requested()) {
    int n = epoll_wait(p->epfd, events, 32, p->has_wake ? -1 : 100);
    for (int e = 0; e < n; e++) {
      size_t slot;
      int fd;
      multi_drain_ctx_t dc;
      if (events[e].data.u64 == LISTEN_MULTI_WAKE_SLOT) continue;
      slot = (size_t)events[e].data.u64;
      fd = p->fds[slot];
      dc = (multi_drain_ctx_t){p, slot};
      drain_socket(fd, buf, sizeof buf, "resolve recv", multi_recv_cb, &dc);
    }
  }
  return NULL;
}

listen_multi_t *listen_multi_start(int family, const char *addr, unsigned base_port, size_t count, listen_multi_cb cb, void *user) {
  listen_multi_t *p;
  size_t opened = 0;
  int rc;
  int wfd;
  struct epoll_event wev;

  if (count == 0) return NULL;
  p = calloc(1, sizeof *p);
  if (!p) return NULL;
  p->fds = calloc(count, sizeof *p->fds);
  if (!p->fds) {
    free(p);
    return NULL;
  }
  p->count = count;
  p->cb = cb;
  p->user = user;

  p->epfd = epoll_create1(0);
  if (p->epfd < 0) {
    log_line(TOOL_NAME ": epoll_create1: %s", strerror(errno));
    free(p->fds);
    free(p);
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    struct epoll_event ev;
    int fd = open_reuseport_socket(family, addr, base_port + (unsigned)i);
    if (fd < 0) goto fail;
    ev.events = EPOLLIN;
    ev.data.u64 = i;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
      log_line(TOOL_NAME ": epoll_ctl: %s", strerror(errno));
      close(fd);
      goto fail;
    }
    p->fds[i] = fd;
    opened++;
  }

  wfd = signal_wake_fd();
  if (wfd >= 0) {
    wev.events = EPOLLIN;
    wev.data.u64 = LISTEN_MULTI_WAKE_SLOT;
    if (epoll_ctl(p->epfd, EPOLL_CTL_ADD, wfd, &wev) == 0)
      p->has_wake = 1;
    else
      log_line(TOOL_NAME ": epoll_ctl (wake fd): %s, falling back to bounded poll", strerror(errno));
  }

  rc = pthread_create(&p->thread, NULL, multi_worker_main, p);
  if (rc != 0) {
    log_line(TOOL_NAME ": pthread_create: %s", strerror(rc));
    goto fail;
  }
  return p;

fail:
  for (size_t i = 0; i < opened; i++) close(p->fds[i]);
  close(p->epfd);
  free(p->fds);
  free(p);
  return NULL;
}

void listen_multi_stop(listen_multi_t *p) {
  if (!p) return;
  pthread_join(p->thread, NULL);
  for (size_t i = 0; i < p->count; i++) close(p->fds[i]);
  close(p->epfd);
  free(p->fds);
  free(p);
}
