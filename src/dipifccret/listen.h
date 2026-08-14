/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_LISTEN_H
#define DIPIFCCRET_LISTEN_H

#include <sys/socket.h>

typedef struct listen_pool listen_pool_t;

/* one datagram received on -l socket; fd/from/fromlen needed for unicast reply */
typedef void (*listen_rtcp_cb)(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user);

/* workers SO_REUSEPORT sockets, thread+epoll each, feeding datagrams to cb. NULL on setup failure = fatal, no retry.
   on partial failure, already-started workers get stopped and joined before returning NULL */
listen_pool_t *listen_pool_start(int family, const char *addr, unsigned port, unsigned workers, listen_rtcp_cb cb, void *user);

/* joins all worker threads (they poll signal_stop_requested()), closes sockets */
void listen_pool_stop(listen_pool_t *p);

typedef struct listen_multi listen_multi_t;

/* slot: see channel_lookup_by_resolve_slot() */
typedef void (*listen_multi_cb)(const unsigned char *pkt, size_t len, size_t slot, int fd, const struct sockaddr *from, socklen_t fromlen, void *user);

/* count sockets, addr:(base_port+i). one thread, one epoll. NULL = fatal, logged. */
listen_multi_t *listen_multi_start(int family, const char *addr, unsigned base_port, size_t count, listen_multi_cb cb, void *user);

void listen_multi_stop(listen_multi_t *p);

#endif
