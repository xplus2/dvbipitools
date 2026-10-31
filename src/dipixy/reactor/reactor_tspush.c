/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* CONN_TSPUSH: server->client only. recv==0 after GET: half-close, not disconnect. write error: peer gone.
   conn_t.slot doubles as ts_sub_t index */

#include "internal.h"
#include "reactor_tls.h"
#include "dispatch/priv.h"
#include "../ts/ts_push.h"

#include <sys/socket.h>

/* CONN_WRITING (headers flushed) -> CONN_TSPUSH, called from reactor_finish().
   c->slot already holds ts_sub_t index */
void reactor_tspush_begin(int epfd, conn_t *c) {
  int ka = 1;
  c->in.len = c->in.off = 0;
  c->next_state = CONN_NEXT_NONE;
  c->close_after_flush = 0;
  c->state = CONN_TSPUSH;
  c->epfd = epfd;
  c->reactor_tid = t_reactor_tid;
  ts_push_set_reactor_tid(c->slot, t_reactor_tid);
  g_ts_subs[c->slot].fd = c->fd;
  setsockopt(c->fd, SOL_SOCKET, SO_KEEPALIVE, &ka, sizeof ka);
  reactor_arm(epfd, c, 0); /* EPOLLIN for FIN detection, EPOLLOUT on demand */
  conn_publish(c);
  atomic_store_explicit(&g_ts_subs[c->slot].ready, 1, memory_order_release);
}

int reactor_tspush_send_head(int sub_idx, const char *content_type) {
  const ts_sub_t *s = &g_ts_subs[sub_idx];
  conn_t *c = conn_for_fd_gen(s->fd, s->conn_gen);
  char header[160];
  size_t header_len;
  if (!c) return -1;
  header_len = build_stream_header(header, sizeof header, content_type, c->ssl != NULL);
  return conn_send_buffered(c, header, header_len, NULL, 0) < 0 ? -1 : 0;
}

void reactor_tspush_close(int epfd, conn_t *c) {
  if (!conn_claim_teardown(c)) return;
  conn_unpublish(c);
  ts_push_unsubscribe_by_idx(c->slot);
  reactor_teardown_common(epfd, c);
}

void reactor_tspush_readable(int epfd, conn_t *c) {
  reactor_push_conn_readable(epfd, c, reactor_tspush_close, reactor_conn_flush, 1);
}
