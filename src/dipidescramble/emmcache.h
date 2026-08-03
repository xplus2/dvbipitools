/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_EMMCACHE_H
#define DIPIDESCRAMBLE_EMMCACHE_H

#include "device.h"

typedef struct emmcache emmcache_t;

/* tracks raw section set backing -e's cache file (one EMM-U + one EMM-G per known service_id) */
emmcache_t *emmcache_new(void);
void emmcache_free(emmcache_t *c);

/* replays saved cache file through device_on_emm(), rehydrating BK/SK and this tracker before any live/-u/--unicast-emm data arrives.
   Missing file: not an error. 0 ok, -1 on read trouble */
int emmcache_load(emmcache_t *c, device_state_t *d, const char *path);

/* feeds 1 reassembled EMM section: device_on_emm(), then records it here on success
   1 if the cache file is now stale (caller should emmcache_save()), else 0 */
int emmcache_feed(emmcache_t *c, device_state_t *d, const unsigned char *emm, size_t emm_len);

/* whole-file rewrite of the current tracked section set. 0 ok, -1 w fail */
int emmcache_save(const emmcache_t *c, const char *path);

#endif
