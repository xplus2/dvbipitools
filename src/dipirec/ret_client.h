/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_RET_CLIENT_H
#define DIPIREC_RET_CLIENT_H

#include <stddef.h>
#include <sys/types.h>

#include "args.h"
#include "lib/net/multicast.h"

typedef struct ret_client ret_client_t;

/* NULL on socket/join failure */
ret_client_t *ret_client_open(const config_t *cfg);

/* seq-ordered TS payload, gaps NACKed and repaired within cfg->ret.wait_ms. >0 len, 0 no data, -1 error */
ssize_t ret_client_read(ret_client_t *r, mcast_t *main, unsigned char *buf, size_t cap);

void ret_client_close(ret_client_t *r);

#endif
