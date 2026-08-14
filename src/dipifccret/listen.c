/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/net/netconnect.h"
#include "lib/signal.h"

#include "listen.h"
#include "version.h"

typedef struct {
  int fd;
  int epfd;
  listen_rtcp_cb cb;
  void *user;
  pthread_t thread;
  atomic_int *stop;
} worker_t;

struct listen_pool {
  worker_t *workers;
  unsigned count;
  atomic_int stop;
};

static void *worker_main(void *arg) {
  worker_t *w = (worker_t *)arg;
  unsigned char buf[2048];

  while (!signal_stop_requested() && !atomic_load_explicit(w->stop, memory_order_relaxed)) {
    struct epoll_event ev;
    int n = epoll_wait(w->epfd, &ev, 1, 100);
    if (n <= 0)
      continue;
    for (;;) {
      struct sockaddr_storage from;
      socklen_t fromlen = sizeof from;
      ssize_t r = recvfrom(w->fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromlen);
      if (r < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
          break;
        if (errno == EINTR)
          continue;
        log_line(TOOL_NAME ": recv: %s", strerror(errno));
        break;
      }
      if (w->cb)
        w->cb(buf, (size_t)r, w->fd, (struct sockaddr *)&from, fromlen, w->user);
    }
  }
  return NULL;
}

static int bind_dgram(int fd, int family, const char *addr, unsigned port) {
  struct sockaddr_storage ss;
  socklen_t sslen;
  if (netaddr_fill(family, addr, port, &ss, &sslen)) {
    log_line(TOOL_NAME ": bad listen address: %s", addr);
    errno = EINVAL; /* netaddr_fill()'s inet_pton() never sets errno; caller's strerror(errno) needs a real value */
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
  unsigned i;
  int fd, epfd, rc;

  if (workers == 0)
    workers = 1;
  p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
  p->workers = calloc(workers, sizeof *p->workers);
  if (!p->workers) {
    free(p);
    return NULL;
  }
  atomic_init(&p->stop, 0);

  for (i = 0; i < workers; i++) {
    struct epoll_event ev;
    worker_t *w = &p->workers[i];

    fd = open_reuseport_socket(family, addr, port);
    if (fd < 0)
      goto fail;
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
  for (i = 0; i < p->count; i++)
    pthread_join(p->workers[i].thread, NULL);
  for (i = 0; i < p->count; i++) {
    close(p->workers[i].epfd);
    close(p->workers[i].fd);
  }
  free(p->workers);
  free(p);
  return NULL;
}

void listen_pool_stop(listen_pool_t *p) {
  unsigned i;
  if (!p)
    return;
  for (i = 0; i < p->count; i++)
    pthread_join(p->workers[i].thread, NULL);
  for (i = 0; i < p->count; i++) {
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
};

static void *multi_worker_main(void *arg) {
  listen_multi_t *p = (listen_multi_t *)arg;
  unsigned char buf[2048];
  struct epoll_event events[32];

  while (!signal_stop_requested()) {
    int n = epoll_wait(p->epfd, events, 32, 100);
    int e;
    for (e = 0; e < n; e++) {
      size_t slot = (size_t)events[e].data.u64;
      int fd = p->fds[slot];
      for (;;) {
        struct sockaddr_storage from;
        socklen_t fromlen = sizeof from;
        ssize_t r = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &fromlen);
        if (r < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
          if (errno == EINTR)
            continue;
          log_line(TOOL_NAME ": resolve recv: %s", strerror(errno));
          break;
        }
        if (p->cb)
          p->cb(buf, (size_t)r, slot, fd, (struct sockaddr *)&from, fromlen, p->user);
      }
    }
  }
  return NULL;
}

listen_multi_t *listen_multi_start(int family, const char *addr, unsigned base_port, size_t count, listen_multi_cb cb, void *user) {
  listen_multi_t *p;
  size_t i, opened = 0;
  int rc;

  if (count == 0)
    return NULL;
  p = calloc(1, sizeof *p);
  if (!p)
    return NULL;
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

  for (i = 0; i < count; i++) {
    struct epoll_event ev;
    int fd = open_reuseport_socket(family, addr, base_port + (unsigned)i);
    if (fd < 0)
      goto fail;
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

  rc = pthread_create(&p->thread, NULL, multi_worker_main, p);
  if (rc != 0) {
    log_line(TOOL_NAME ": pthread_create: %s", strerror(rc));
    goto fail;
  }
  return p;

fail:
  for (i = 0; i < opened; i++)
    close(p->fds[i]);
  close(p->epfd);
  free(p->fds);
  free(p);
  return NULL;
}

void listen_multi_stop(listen_multi_t *p) {
  size_t i;
  if (!p)
    return;
  pthread_join(p->thread, NULL);
  for (i = 0; i < p->count; i++)
    close(p->fds[i]);
  close(p->epfd);
  free(p->fds);
  free(p);
}
