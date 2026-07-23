/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "listen.h"

typedef struct {
  int fd;
  int epfd;
  ret_ctx_t *r;
  pthread_t thread;
} worker_t;

struct listen_pool {
  worker_t *workers;
  unsigned count;
};

static void *worker_main(void *arg) {
  worker_t *w = (worker_t *)arg;
  unsigned char buf[2048];

  while (!signal_stop_requested()) {
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
        log_line("dipiret: recv: %s", strerror(errno));
        break;
      }
      ret_on_rtcp(w->r, buf, (size_t)r, w->fd, (struct sockaddr *)&from, fromlen);
    }
  }
  return NULL;
}

static int bind_dgram(int fd, int family, const char *addr, unsigned port) {
  if (family == AF_INET) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, addr, &a.sin_addr) != 1) {
      log_line("dipiret: bad listen address: %s", addr);
      return -1;
    }
    return bind(fd, (struct sockaddr *)&a, sizeof a);
  } else {
    struct sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons((unsigned short)port);
    if (inet_pton(AF_INET6, addr, &a.sin6_addr) != 1) {
      log_line("dipiret: bad listen address: %s", addr);
      return -1;
    }
    return bind(fd, (struct sockaddr *)&a, sizeof a);
  }
}

static int open_reuseport_socket(int family, const char *addr, unsigned port) {
  int fd, on = 1;

  fd = socket(family, SOCK_DGRAM, 0);
  if (fd < 0) {
    log_line("dipiret: socket: %s", strerror(errno));
    return -1;
  }
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on) < 0) {
    log_line("dipiret: SO_REUSEPORT: %s", strerror(errno));
    close(fd);
    return -1;
  }
  if (bind_dgram(fd, family, addr, port) < 0) {
    log_line("dipiret: bind: %s", strerror(errno));
    close(fd);
    return -1;
  }
  return fd;
}

listen_pool_t *listen_pool_start(int family, const char *addr, unsigned port, unsigned workers, ret_ctx_t *r) {
  listen_pool_t *p;
  unsigned i;

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

  for (i = 0; i < workers; i++) {
    struct epoll_event ev;
    worker_t *w = &p->workers[i];

    w->fd = open_reuseport_socket(family, addr, port);
    if (w->fd < 0) {
      free(p->workers);
      free(p);
      return NULL;
    }
    w->epfd = epoll_create1(0);
    if (w->epfd < 0) {
      log_line("dipiret: epoll_create1: %s", strerror(errno));
      close(w->fd);
      free(p->workers);
      free(p);
      return NULL;
    }
    ev.events = EPOLLIN;
    ev.data.fd = w->fd;
    if (epoll_ctl(w->epfd, EPOLL_CTL_ADD, w->fd, &ev) < 0) {
      log_line("dipiret: epoll_ctl: %s", strerror(errno));
      close(w->epfd);
      close(w->fd);
      free(p->workers);
      free(p);
      return NULL;
    }
    w->r = r;
    if (pthread_create(&w->thread, NULL, worker_main, w) != 0) {
      log_line("dipiret: pthread_create: %s", strerror(errno));
      close(w->epfd);
      close(w->fd);
      free(p->workers);
      free(p);
      return NULL;
    }
    p->count = i + 1;
  }
  return p;
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
