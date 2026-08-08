/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_IPICLIENT_H
#define DIPIDESCRAMBLE_IPICLIENT_H

#include "emmcache.h"

typedef struct ipiclient ipiclient_t;

/* uri: full -u/--unicast-emm uri incl. userinfo token, e.g. https://<token>@<host>:<port>/device/<serial>/emm. NULL = malformed */
ipiclient_t *ipiclient_new(const char *uri, int insecure);
void ipiclient_free(ipiclient_t *c);

/* one-shot fetch (ETag conditional), feeds returned units through emmcache_feed().
   No poll loop. 1 if cache is now stale (caller should emmcache_save()), else 0 */
int ipiclient_poll(ipiclient_t *c, emmcache_t *cache, device_state_t *d);

#endif
