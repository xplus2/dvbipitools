/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRET_LISTEN_H
#define DIPIRET_LISTEN_H

#include "ret.h"

typedef struct listen_pool listen_pool_t;

/* workers SO_REUSEPORT sockets, thread+epoll each, feeding datagrams to ret_on_rtcp. NULL on
   setup failure (logged) - fatal, do not retry; may leave earlier workers running, exit instead */
listen_pool_t *listen_pool_start(int family, const char *addr, unsigned port, unsigned workers, ret_ctx_t *r);

void listen_pool_stop(listen_pool_t *p); /* joins all worker threads (they poll signal_stop_requested()), closes sockets */

#endif
