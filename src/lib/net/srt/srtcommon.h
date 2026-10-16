/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTCOMMON_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTCOMMON_H

#include <stddef.h>
#include <sys/socket.h>

#include <srt/srt.h>
#include <srt/logging_api.h>

#define SRTCOMMON_MAX_PEERS 8

typedef enum { SRTGROUP_NONE, SRTGROUP_BROADCAST, SRTGROUP_BACKUP } srtgroup_mode_t;

typedef struct {
  const char *host; /* hostname or numeric IP */
  unsigned port;
} srtcommon_peer_t;

typedef struct {
  const char *passphrase;   /* NULL/"" = no encryption */
  int pbkeylen;              /* 0 = library default (16); else 16/24/32 */
  const char *streamid;      /* NULL/"" = none */
  const char *packetfilter;  /* NULL/"" = none; raw SRTO_PACKETFILTER string */
  unsigned latency_ms;        /* 0 = library default; sets SRTO_LATENCY (both directions) */
} srtcommon_opts_t;

void srtcommon_open_logging(int verbose);

/* refcounted srt_startup()/srt_cleanup(). 0 ok, -1 error */
int srtcommon_startup(void);
void srtcommon_cleanup(void);

/* is_group skips TRANSTYPE: rejected on group sockets, live forced already.
   timeo_optname/timeo_ms: SRTO_SNDTIMEO or SRTO_RCVTIMEO, direction-specific. 0 ok, -1 error */
int srtcommon_apply_opts(SRTSOCKET s, const srtcommon_opts_t *o, int is_group, int timeo_optname, int timeo_ms);

/* SOCK_DGRAM: addrinfo family/form only. UDP socket itself: caller's own. 0 ok, -1 error */
int srtcommon_resolve(const char *host, unsigned port, struct sockaddr_storage *ss, int *len);

/* resolve on detached helper, abandoned on stop()/timeout (helper frees itself when the resolver returns).
   timeout_ms 0: no limit. at most 16 in flight process-wide. 0 ok, -1 err/stopped */
int srtcommon_resolve_stoppable(const char *host, unsigned port, struct sockaddr_storage *ss, int *len, unsigned timeout_ms, int (*stop)(const void *), const void *arg);

/* non-blocking resolve of n (<= SRTCOMMON_MAX_PEERS + 1) hosts on detached helpers. NULL: error.
   poll: 1 once every lookup settled. get: 0 ok, -1 failed or unsettled. free abandons unsettled lookups */
typedef struct srtcommon_resolver srtcommon_resolver_t;
srtcommon_resolver_t *srtcommon_resolver_start(const srtcommon_peer_t *peers, int n);
int srtcommon_resolver_poll(srtcommon_resolver_t *r);
int srtcommon_resolver_get(const srtcommon_resolver_t *r, int i, struct sockaddr_storage *ss, int *len);
void srtcommon_resolver_free(srtcommon_resolver_t *r);

/* builds one SRT_SOCKGROUPCONFIG per peer via srt_prepare_endpoint(). 0 ok, -1 error (resolve failed) */
int srtcommon_build_group_config(const srtcommon_peer_t *peers, int npeers, SRT_SOCKGROUPCONFIG *gc_out);

/* peers (addrs[i], resolved) without live member in data[0..ndata). missing[i]=1 for each.
   addrs[i] family 0: unresolved, never missing. returns number missing */
int srtcommon_group_missing(const struct sockaddr_storage *addrs, int npeers, const SRT_SOCKGROUPDATA *data, size_t ndata, int *missing);

/* re-adds configured peers without live member to caller group g. never blocks: lookups run on
   helpers, *res holds them across calls (init NULL, call again until NULL). libsrt leaves
   reconnecting broken members to the application. 0 ok/nothing to do/pending, -1 error */
int srtcommon_group_revive(SRTSOCKET g, const srtcommon_peer_t *peers, int npeers, srtcommon_resolver_t **res);

#endif
