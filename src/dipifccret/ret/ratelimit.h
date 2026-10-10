/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIFCCRET_RATELIMIT_H
#define DIPIFCCRET_RATELIMIT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

/* keyed by IP (v6: /64), not port. direct-mapped: collision resets bucket */
typedef struct ratelimit ratelimit_t;

ratelimit_t *ratelimit_new(size_t slots);
void ratelimit_free(ratelimit_t *rl);

/* returns granted <= want. rate 0 = unlimited */
unsigned ratelimit_take(ratelimit_t *rl, const struct sockaddr *addr, unsigned rate, unsigned burst, unsigned want, int64_t now_ms);

#endif
