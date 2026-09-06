/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_IPICLIENT_H
#define DIPIDESCRAMBLE_IPICLIENT_H

#include "emmcache.h"

typedef struct ipiclient ipiclient_t;

/* uri: full -u/--unicast-emm uri incl. userinfo token, e.g. https://<token>@<host>:<port>/device/<serial>/emm.
   token_header: HTTP header for token, NULL/empty = "X-Device-Token". NULL return = malformed */
ipiclient_t *ipiclient_new(const char *uri, int insecure, const char *token_header);
void ipiclient_free(ipiclient_t *c);

typedef struct ipiclient_poll ipiclient_poll_t;
typedef enum { IPICLIENT_POLL_PENDING, IPICLIENT_POLL_DONE, IPICLIENT_POLL_ERROR } ipiclient_poll_state_t;

ipiclient_poll_t *ipiclient_poll_start(ipiclient_t *c);
int ipiclient_poll_fd(const ipiclient_poll_t *p);
short ipiclient_poll_events(const ipiclient_poll_t *p);
ipiclient_poll_state_t ipiclient_poll_step(ipiclient_poll_t *p);
int ipiclient_poll_take(ipiclient_poll_t *p, emmcache_t *cache, device_state_t *d);
void ipiclient_poll_free(ipiclient_poll_t *p);

#endif
