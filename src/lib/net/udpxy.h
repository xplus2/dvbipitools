/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_NET_UDPXY_H
#define DIPIREC_NET_UDPXY_H

#include <stddef.h>
#include <sys/types.h>

typedef struct udpxy udpxy_t;

/* connect + GET; positioned at TS body */
udpxy_t *udpxy_open(const char *host, unsigned port, const char *path, const char *user_agent);

/* body bytes. >0 len, 0 timeout, -1 closed */
ssize_t udpxy_read(udpxy_t *u, void *buf, size_t cap);

void udpxy_close(udpxy_t *u);

#endif
