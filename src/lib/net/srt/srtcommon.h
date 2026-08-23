/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTCOMMON_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTCOMMON_H

#include <stddef.h>
#include <sys/socket.h>

#include <srt/srt.h>

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

/* is_group skips TRANSTYPE: rejected on group sockets, live forced already.
   timeo_optname/timeo_ms: SRTO_SNDTIMEO or SRTO_RCVTIMEO, direction-specific. 0 ok, -1 error */
int srtcommon_apply_opts(SRTSOCKET s, const srtcommon_opts_t *o, int is_group, int timeo_optname, int timeo_ms);

/* SOCK_DGRAM: addrinfo family/form only. UDP socket itself: caller's own. 0 ok, -1 error */
int srtcommon_resolve(const char *host, unsigned port, struct sockaddr_storage *ss, int *len);

/* builds one SRT_SOCKGROUPCONFIG per peer via srt_prepare_endpoint(). 0 ok, -1 error (resolve failed) */
int srtcommon_build_group_config(const srtcommon_peer_t *peers, int npeers, SRT_SOCKGROUPCONFIG *gc_out);

#endif
