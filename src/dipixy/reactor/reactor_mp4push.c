/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "internal.h"
#include "reactor_tls.h"
#include "../segment/mp4push.h"

#include <sys/epoll.h>
#include <sys/socket.h>

void reactor_mp4push_begin(int epfd, conn_t *c) {
  c->in.off = 0;
  c->in.len = 0;
  c->become_mp4push = 0;
  c->close_after_flush = 0;
  c->state = CONN_MP4PUSH;
  c->epfd = epfd;
  c->reactor_tid = t_reactor_tid;
  {
    int ka = 1;
    setsockopt(c->fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
  }
  reactor_arm(epfd, c, 0);
  conn_publish(c);
}

void reactor_mp4push_close(int epfd, conn_t *c) {
  if (!conn_claim_teardown(c)) return;
  conn_unpublish(c);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  mp4push_sub_close(c->slot);
  tls_close_fd(c->fd);
  conn_free(c);
}

void reactor_mp4push_readable(int epfd, conn_t *c) {
  reactor_push_conn_readable(epfd, c, reactor_mp4push_close, reactor_mp4push_flush);
}

void reactor_mp4push_flush(int epfd, conn_t *c) {
  int rc;
  int dead;
  pthread_mutex_lock(&c->out_lock);
  dead = c->dead;
  pthread_mutex_unlock(&c->out_lock);
  rc = dead ? CONN_FLUSH_ERROR : conn_flush(c, epfd);
  if (rc == CONN_FLUSH_ERROR)
    reactor_mp4push_close(epfd, c);
}
