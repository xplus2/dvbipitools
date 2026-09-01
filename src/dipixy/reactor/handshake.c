/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* TCP accept + TLS handshake, then route by ALPN (h1 vs h2) */

#define _GNU_SOURCE
#include "internal.h"
#include "reactor.h"
#include "reactor_tls.h"
#ifdef HAVE_HTTP2
#include "../http2/http2.h"
#endif

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

/* post-handshake: route by ALPN. h2 -> nghttp2 session (fd stays in epoll). h1 -> normal read */
static void reactor_after_handshake(int epfd, conn_t *c) {
#ifdef HAVE_HTTP2
  if (tls_alpn_is_h2(c->fd)) {
    c->epfd = epfd;
    if (h2_conn_attach(c) < 0)
      reactor_close(epfd, c);
    return;
  }
#endif
  c->state = CONN_READING;
  reactor_read(epfd, c);
}

/* drives handshake, ALPN known before app data */
void reactor_handshake(int epfd, conn_t *c) {
  int r = tls_handshake(c->fd);
  if (r == 0) {
    reactor_arm(epfd, c, t_tls_want_write);
    return;
  } /* need more I/O */
  if (r < 0) {
    reactor_close(epfd, c);
    return;
  } /* failed */
  reactor_after_handshake(epfd, c);
}

/* accept finish: sockopts, conn_new, client_ip, epoll add. shared by io_uring + accept4() paths */
static void reactor_accept_setup(int epfd, const reactor_listener *L, int fd, const struct sockaddr_storage *saddr) {
  __atomic_add_fetch(&g_connections_total, 1, __ATOMIC_RELAXED);
  int nd = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
  if (L->is_tls && tls_accept(fd) < 0) { /* SSL setup, lazy handshake */
    close(fd);
    return;
  }
  if (!L->is_tls) {
    int zc = 1;
    setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &zc, sizeof(zc));
  }
  conn_t *c = conn_new(fd, L->is_tls ? (void *)1 : NULL);
  if (!c) {
    tls_close_fd(fd);
    return;
  }
  c->epfd = epfd; /* owning epoll, for cross-thread EPOLLOUT arming */
  if (saddr->ss_family == AF_INET6)
    inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)saddr)->sin6_addr, c->client_ip, sizeof(c->client_ip));
  else
    inet_ntop(AF_INET, &((const struct sockaddr_in *)saddr)->sin_addr, c->client_ip, sizeof(c->client_ip));

  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = EPOLLIN;
  ev.data.ptr = c;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    reactor_close(epfd, c);
}

/* listener readable: accept all pending */
void reactor_accept(int epfd, reactor_listener *L) {
  for (;;) {
    struct sockaddr_storage saddr;
    socklen_t addrlen = sizeof(saddr);
    int fd = accept4(L->fd, (struct sockaddr *)&saddr, &addrlen,SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
      break; /* EAGAIN: backlog drained (or a transient error) */
    reactor_accept_setup(epfd, L, fd, &saddr);
  }
}
