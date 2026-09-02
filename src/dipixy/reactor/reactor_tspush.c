/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* CONN_TSPUSH: server->client only. recv==0 after GET: half-close, not disconnect. write error: peer gone.
   conn_t.slot doubles as ts_sub_t index */

#include "internal.h"
#include "reactor_tls.h"
#include "../ts/ts_push.h"

#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>

/* CONN_WRITING (headers flushed) -> CONN_TSPUSH, called from reactor_finish()
   when become_tspush set. c->slot already holds ts_sub_t index */
void reactor_tspush_begin(int epfd, conn_t *c) {
  c->in.len = c->in.off = 0;
  c->become_tspush = 0;
  c->close_after_flush = 0;
  c->state = CONN_TSPUSH;
  c->epfd = epfd;
  c->reactor_tid = t_reactor_tid;
  ts_push_set_reactor_tid(c->slot, t_reactor_tid);
  g_ts_subs[c->slot].fd = c->fd;
  {
    int ka = 1;
    setsockopt(c->fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
  }
  reactor_arm(epfd, c, 0); /* EPOLLIN for FIN detection, EPOLLOUT on demand */
  conn_publish(c);
  atomic_store_explicit(&g_ts_subs[c->slot].ready, 1, memory_order_release);
}

void reactor_tspush_close(int epfd, conn_t *c) {
  if (!conn_claim_teardown(c))
    return;
  conn_unpublish(c);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  ts_push_unsubscribe_by_idx(c->slot);
  tls_close_fd(c->fd);
  conn_free(c);
}

void reactor_tspush_readable(int epfd, conn_t *c) {
  char buf[256];
  for (;;) {
    ssize_t n = tls_net_recv(c->fd, buf, sizeof buf);
    if (n > 0)
      continue; /* drain unexpected data */
    if (n == 0) {
      atomic_store_explicit(&c->read_done, 1, memory_order_relaxed);
      conn_epoll_mod(c, epfd, c->want_write); /* epoll reports FIN forever: stop polling */
      return; /* half-close: not a disconnect */
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    reactor_tspush_close(epfd, c);
    return;
  }
  reactor_conn_flush(epfd, c);
}
