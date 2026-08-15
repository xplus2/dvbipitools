/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_NETCONNECT_H
#define DVBIPITOOLS_LIB_NET_NETCONNECT_H

#include <sys/socket.h>

/* bounded network-failure classification, shared by every read/connect path
   in lib/net and reused as-is for metrics labels (lib/metrics depends on
   this, not the other way around) */
typedef enum {
  NET_ERR_DNS,
  NET_ERR_CONNECT,
  NET_ERR_TIMEOUT,
  NET_ERR_TLS,
  NET_ERR_HTTP,
  NET_ERR_FORMAT,
  NET_ERR_READ,
  NET_ERR_EOF,
  NET_ERR_OTHER,
  NET_ERR_COUNT
} net_err_reason_t;

const char *net_err_reason_name(net_err_reason_t reason);

/* nonblocking connect+poll, avoids multi-minute blocking-connect ceiling.
   fd stays O_NONBLOCK. -1 on failure/timeout/stop (logged, errno set).
   reason_out: nullable, set only on -1 */
int netconnect_tcp(const char *host, unsigned port, int timeout_ms, net_err_reason_t *reason_out);

/* sockaddr_storage from family+addr:port via inet_pton. 0 ok, -1 bad addr (not logged) */
int netaddr_fill(int family, const char *addr, unsigned port, struct sockaddr_storage *ss, socklen_t *sslen);

/* IP_TOS (v4) or IPV6_TCLASS (v6) on fd */
int net_set_dscp(int fd, int family, int tos);

/* holds the resolved addrinfo list + position for netconnect_tcp_start/finish's
   fallback to the next address when one fails */
typedef struct netconnect_pending netconnect_pending_t;

/* async pair: for callers running their own poll() loop over many connections at once (netconnect_tcp's own wait_connect() would serialize them).
   resolves host, starts connect() on first usable address, fd stays O_NONBLOCK. does not wait.
   *pending_out receives remaining-address state on success, pass it to netconnect_tcp_finish().
   -1 on immediate failure (*pending_out untouched), else poll fd for POLLOUT, then call netconnect_tcp_finish().
   reason_out: nullable, set only on -1 */
int netconnect_tcp_start(const char *host, unsigned port, netconnect_pending_t **pending_out, net_err_reason_t *reason_out);

/* call once *fd is POLLOUT-ready. 1: connected, *pending freed and set NULL, *fd unchanged.
   0: this address failed, old *fd closed internally, next candidate from the same resolved
   list opened and connecting, *fd updated to it - poll *fd for POLLOUT and call again.
   -1: every address failed (not logged, caller's job), *pending freed and set NULL, *fd
   closed and set to -1, errno set. reason_out: nullable, set only on -1 */
int netconnect_tcp_finish(netconnect_pending_t **pending, int *fd, net_err_reason_t *reason_out);

/* give up on a pending connect before netconnect_tcp_finish() reaches a terminal result
   (1 or -1). frees the remaining-address state; does not touch fd, caller closes that itself. */
void netconnect_tcp_abort(netconnect_pending_t *pending);

#endif
