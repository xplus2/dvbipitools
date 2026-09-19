/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* HTTP/3 server: QUIC via ngtcp2, HTTP/3 framing via nghttp3. no server push */

#ifdef HAVE_HTTP3

#include "http3.h"
#include "http3_int.h"

#include <sys/socket.h>
#include <unistd.h>

void h3_thread_cleanup(void) {
  /* releases live QUIC conns at shutdown */
  if (t_h3_init) {
    while (t_h3_active_cnt > 0) h3conn_del(t_h3_active[0]); /* swap-remove compacts to index 0 */
    h3_tables_free();
  }
  h3_udp_free();
}

static void handle_dgram(int udp_fd, const h3_rx_t *rx, const struct sockaddr_storage *local, socklen_t local_len, h3_conn_t **touched, int *ntouched) {
  const struct sockaddr *peer = (const struct sockaddr *)&rx->peer;
  h3_conn_t *c = find_conn(rx->data, rx->len);
  if (!c) {
    ngtcp2_pkt_hd hd;
    h3_admit_t ai;
    if (!h3_stateless_admit(udp_fd, rx->data, rx->len, peer, rx->peerlen, t_h3_active_cnt, g_h3_max_conns, &hd, &ai)) return;
    c = h3conn_new(rx->data, rx->len, peer, rx->peerlen, (const struct sockaddr *)local, local_len, &ai);
    if (!c) return;
  }
  /* ts sampled after conn: h3conn_new() seeds ngtcp2 initial_ts, read_pkt ts >= that (ngtcp2 monotonic) */
  ngtcp2_tstamp ts = h3_ts();
  c->last_rx = ts;
  ngtcp2_path_storage ps;
  ngtcp2_path_storage_zero(&ps);
  ngtcp2_addr_init(&ps.path.local, (const struct sockaddr *)local, local_len);
  ngtcp2_addr_init(&ps.path.remote, peer, rx->peerlen);
  ngtcp2_pkt_info pi = {0};
  if (ngtcp2_conn_read_pkt_versioned(c->qconn, &ps.path, NGTCP2_PKT_INFO_VERSION, &pi, rx->data, rx->len, ts) < 0) c->done = 1;
  if (!c->done && c->h3conn) for (int i = 0; i < c->max_reqs; i++) {
    if (c->reqs[i].active && c->reqs[i].dispatch_pending) {
      c->reqs[i].dispatch_pending = 0;
      dispatch_req(c, &c->reqs[i]);
    }
  }
  if (!c->done && !c->tx_dirty) {
    c->tx_dirty = 1;
    touched[(*ntouched)++] = c;
  }
}

void h3_handle_readable(int udp_fd) {
  if (!h3_tables_alloc()) return;
  h3_rx_t rx[H3_RX_BATCH];
  h3_conn_t *touched[H3_RX_BATCH];
  struct sockaddr_storage local_addr;
  socklen_t local_len;
  if (udp_fd == t_h3_udp4) {
    local_addr = t_h3_udp4_local;
    local_len = t_h3_udp4_local_len;
  } else {
    local_addr = t_h3_udp6_local;
    local_len = t_h3_udp6_local_len;
  }

  /* drain socket until short batch, not datagram per wake */
  for (;;) {
    int ntouched = 0;
    int n = h3_udp_recv(udp_fd, rx, H3_RX_BATCH);
    if (n <= 0) break;
    for (int i = 0; i < n; i++)
      if (rx[i].len > 0) handle_dgram(udp_fd, &rx[i], &local_addr, local_len, touched, &ntouched);
    for (int i = 0; i < ntouched; i++) {
      touched[i]->tx_dirty = 0;
      if (!touched[i]->done) flush_tx(touched[i], udp_fd);
    }
    if (n < H3_RX_BATCH) break;
  }

  for (int i = 0; i < t_h3_active_cnt; ) {
    if (t_h3_active[i]->done) h3conn_del(t_h3_active[i]); /* swap-remove: recheck index, don't advance */
    else i++;
  }
}

/* ms until nearest QUIC timer across this thread's conns, for epoll_wait sizing. -1 = no active HTTP/3 conns. */
int h3_next_timeout_ms(void) {
  if (!t_h3_init || t_h3_active_cnt == 0) return -1;
  ngtcp2_tstamp now = h3_ts();
  ngtcp2_tstamp nearest = UINT64_MAX;
  for (int i = 0; i < t_h3_active_cnt; i++) {
    ngtcp2_tstamp expiry = ngtcp2_conn_get_expiry(t_h3_active[i]->qconn);
    if (expiry < nearest) nearest = expiry;
  }
  if (nearest <= now) return 0;
  uint64_t ms = (nearest - now) / 1000000ULL;
  return ms > 1000 ? 1000 : (int)ms; /* cap: idle timeouts still get checked */
}

void h3_tick(void) {
  if (!t_h3_init) return;
  ngtcp2_tstamp now = h3_ts();
  for (int i = 0; i < t_h3_active_cnt; ) {
    h3_conn_t *c = t_h3_active[i];
    if (now - c->last_rx > g_h3_idle_ns) {
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
      if (fd >= 0) flush_tx(c, fd);
    }
    i++;
  }
}

#endif /* HAVE_HTTP3 */
