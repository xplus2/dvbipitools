/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* CONN_DASHCHUNK: server->client only, chunk bytes arrive via conn_send_buffered
   from the segmenter thread. bounded, unlike CONN_TSPUSH: resumes normal
   keep-alive once the lldash subscriber (c->slot) reports finalized. */

#include "internal.h"
#include "reactor_tls.h"
#include "../dash/lldash.h"

#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>

void reactor_dashchunk_begin(int epfd, conn_t *c) {
  c->in.len = c->in.off = 0;
  c->become_dashchunk = 0;
  c->close_after_flush = 0;
  c->state = CONN_DASHCHUNK;
  c->epfd = epfd;
  c->reactor_tid = t_reactor_tid;
  reactor_arm(epfd, c, 0);
  conn_publish(c);
}

void reactor_dashchunk_close(int epfd, conn_t *c) {
  if (!conn_claim_teardown(c)) return;
  dash_lldash_sub_close(c->slot);
  conn_unpublish(c);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  tls_close_fd(c->fd);
  conn_free(c);
}

void reactor_dashchunk_readable(int epfd, conn_t *c) {
  char buf[256];
  for (;;) {
    ssize_t n = tls_net_recv(c->fd, buf, sizeof buf);
    if (n > 0) continue;
    if (n == 0) {
      atomic_store_explicit(&c->read_done, 1, memory_order_relaxed);
      conn_epoll_mod(c, epfd, c->want_write);
      return; /* half close: no disconn */
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    reactor_dashchunk_close(epfd, c);
    return;
  }
  reactor_dashchunk_flush(epfd, c);
}

void reactor_dashchunk_flush(int epfd, conn_t *c) {
  int rc, close_after, keep;
  pthread_mutex_lock(&c->out_lock);
  rc = c->dead ? CONN_FLUSH_ERROR : conn_flush(c, epfd);
  close_after = c->close_after_flush;
  keep = c->keep_alive;
  pthread_mutex_unlock(&c->out_lock);
  if (rc == CONN_FLUSH_ERROR) {
    reactor_dashchunk_close(epfd, c);
    return;
  }
  if (rc == CONN_FLUSH_MORE) return;
  if (!dash_lldash_sub_finalized(c->slot)) return;
  dash_lldash_sub_close(c->slot);
  conn_unpublish(c); /* published for cross-thread conn_for_fd() */
  if (close_after || !keep) {
    reactor_close(epfd, c);
    return;
  }
  reactor_keepalive(epfd, c);
}
