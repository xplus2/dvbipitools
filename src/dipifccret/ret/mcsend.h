/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_MCSEND_H
#define DIPIFCCRET_MCSEND_H

#include <stddef.h>

#include "../channel/channel.h"
#include "lib/net/multicast.h"

typedef struct mcsend_table mcsend_table_t;

/* max_channels must match channel_table_t it's paired with, or entries can run out before channels do */
mcsend_table_t *mcsend_table_new(size_t max_channels, const char *iface, int ttl);
void mcsend_table_free(mcsend_table_t *t);

/* single writer only (capture thread); opens c's MC RET socket if not already open, port 0 = reuse c's own (F.6.2.2) */
void mcsend_ensure(mcsend_table_t *t, channel_t *c, unsigned ff_port);

/* lock-free, any thread; NULL if mcsend_ensure hasn't run for c yet */
mcast_t *mcsend_get(mcsend_table_t *t, const channel_t *c);

#endif
