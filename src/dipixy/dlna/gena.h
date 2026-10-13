/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_GENA_H
#define DIPIXY_DLNA_GENA_H

#include <stddef.h>

#include "../cli/args.h"

typedef enum { GENA_SVC_CD, GENA_SVC_CM } gena_svc_t;

/* new (sid empty) or renewal. header args NULL/"" if absent. peer_ip: requester's IP text.
   callback: http://ip-literal URL, must equal peer_ip. SID usable by creating peer only.
   200 ok. 400 bad header mix. 412 bad NT/CALLBACK/peer or unknown SID. 503 table or per-peer limit full */
int gena_subscribe(const config_t *cfg, gena_svc_t svc, const char *peer_ip, const char *sid, const char *callback, const char *nt, const char *timeout, char *out_sid, size_t out_sidsz, unsigned *out_timeout_s);

/* 200 ok. 400 NT/CALLBACK present. 412 missing or unknown SID (or other peer) */
int gena_unsubscribe(gena_svc_t svc, const char *peer_ip, const char *sid, const char *callback, const char *nt);

/* bump SystemUpdateID, NOTIFY to cd subscribers */
void gena_notify_system_update(void);

/* never 0. time-derived start: differs across restarts */
unsigned gena_system_update_id(void);

/* joins delivery thread, drop subscriptions. safe if never started */
void gena_stop(void);

#endif
