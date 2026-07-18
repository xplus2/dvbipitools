/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_NET_MULTICAST_H
#define DIPIREC_NET_MULTICAST_H

#include <stddef.h>
#include <sys/types.h>

typedef struct mcast mcast_t;

/* join group:port (ASM) on iface, NULL = kernel default. recv_timeout_ms bounds mcast_recv()'s wait */
mcast_t *mcast_open(int family, const char *group, unsigned port, const char *iface, int recv_timeout_ms);

/* one datagram. >0 len, 0 timeout, -1 error */
ssize_t mcast_recv(mcast_t *m, void *buf, size_t cap);

/* leave and close */
void mcast_close(mcast_t *m);

#endif
