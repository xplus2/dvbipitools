/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HTTP/3 server: QUIC via ngtcp2, HTTP/3 framing via nghttp3. no connection
   migration, no server push */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"

#include <sys/socket.h>
#include <unistd.h>

void h3_thread_cleanup(void) {
  /* releases live QUIC conns at shutdown */
  if (t_h3_init)
    while (t_h3_active_cnt > 0)
      h3conn_del(t_h3_active[0]); /* swap-remove compacts to index 0 */
}

void h3_handle_readable(int udp_fd) {
  if (!h3_tables_alloc())
    return;
  uint8_t buf[H3_PKT_MAX];
  struct sockaddr_storage peer_addr, local_addr;

  socklen_t local_len = sizeof local_addr;
  if (getsockname(udp_fd, (struct sockaddr *)&local_addr, &local_len) < 0)
    return;

  /* drain socket: loop until EAGAIN, not one datagram per wakeup */
  for (;;) {
    socklen_t peer_len = sizeof peer_addr;
    ssize_t nread = recvfrom(udp_fd, buf, sizeof buf, 0, (struct sockaddr *)&peer_addr, &peer_len);
    if (nread <= 0)
      break; /* drained */

    h3_conn_t *c = find_conn(buf, (size_t)nread);
    if (!c) {
      c = h3conn_new(buf, (size_t)nread, (struct sockaddr *)&peer_addr, peer_len, (struct sockaddr *)&local_addr, local_len);
      if (!c)
        continue;
    }
    /* ts must be sampled after conn exists: h3conn_new() seeds ngtcp2 initial_ts, read_pkt ts must be >= that (ngtcp2 asserts monotonic) */
    ngtcp2_tstamp ts = h3_ts();
    c->last_rx = ts;

    ngtcp2_path_storage ps;
    ngtcp2_path_storage_zero(&ps);
    ngtcp2_addr_init(&ps.path.local, (struct sockaddr *)&local_addr, local_len);
    ngtcp2_addr_init(&ps.path.remote, (struct sockaddr *)&peer_addr, peer_len);

    ngtcp2_pkt_info pi = {0};
    int rprc = ngtcp2_conn_read_pkt_versioned(c->qconn, &ps.path, NGTCP2_PKT_INFO_VERSION, &pi, buf, (size_t)nread, ts);
    if (rprc < 0)
      c->done = 1;

    if (!c->done && c->h3conn) {
      for (int i = 0; i < H3_MAX_REQS; i++) {
        if (c->reqs[i].active && c->reqs[i].dispatch_pending) {
          c->reqs[i].dispatch_pending = 0;
          dispatch_req(c, &c->reqs[i]);
        }
      }
    }

    if (!c->done)
      flush_tx(c, udp_fd);
  }

  for (int i = 0; i < t_h3_active_cnt; ) {
    if (t_h3_active[i]->done)
      h3conn_del(t_h3_active[i]); /* swap-remove: recheck index, don't advance */
    else
      i++;
  }
}

/* ms until nearest QUIC timer across this thread's conns, for epoll_wait sizing. -1 = no active HTTP/3 conns. */
int h3_next_timeout_ms(void) {
  if (!t_h3_init || t_h3_active_cnt == 0)
    return -1;
  ngtcp2_tstamp now = h3_ts();
  ngtcp2_tstamp nearest = UINT64_MAX;
  for (int i = 0; i < t_h3_active_cnt; i++) {
    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(t_h3_active[i]->qconn);
    if (expiry < nearest)
      nearest = expiry;
  }
  if (nearest <= now)
    return 0;
  uint64_t ms = (nearest - now) / 1000000ULL;
  return ms > 1000 ? 1000 : (int)ms; /* cap: idle timeouts still get checked */
}

void h3_tick(void) {
  if (!t_h3_init)
    return;
  ngtcp2_tstamp now = h3_ts();

  for (int i = 0; i < t_h3_active_cnt; ) {
    h3_conn_t *c = t_h3_active[i];
    if (now - c->last_rx > H3_IDLE_NS) {
      c->done = 1;
      h3conn_del(c); /* swap-remove: recheck index, don't advance */
      continue;
    }
    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(c->qconn);
    if (expiry <= now) {
      if (ngtcp2_conn_handle_expiry(c->qconn, now) != 0) {
        c->done = 1;
        h3conn_del(c);
        continue;
      }
      int fd = c->local_addr.ss_family == AF_INET6 ? t_h3_udp6 : t_h3_udp4;
      if (fd >= 0)
        flush_tx(c, fd);
    }
    i++;
  }
}

#endif /* HAVE_HTTP3 */
