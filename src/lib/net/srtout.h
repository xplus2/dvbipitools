/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRTOUT_H
#define DVBIPITOOLS_LIB_NET_SRTOUT_H

#include <stddef.h>

#include "lib/metrics/export.h"

#define SRTOUT_MAX_PEERS 8

typedef enum { SRTOUT_GROUP_NONE, SRTOUT_GROUP_BROADCAST, SRTOUT_GROUP_BACKUP } srtout_group_mode_t;

typedef struct {
  const char *host; /* hostname or numeric IP */
  unsigned port;
} srtout_peer_t;

typedef struct {
  srtout_peer_t peers[SRTOUT_MAX_PEERS];
  int npeers;                 /* 1: plain connection. >1: requires group_mode != NONE */
  srtout_group_mode_t group_mode; /* NONE: plain srt_connect()/srt_rendezvous() to peers[0] */
  int rendezvous;              /* srt_rendezvous() instead of srt_connect(); group_mode must be NONE */
  const char *local_host;       /* NULL = wildcard. required when rendezvous set */
  unsigned local_port;          /* 0 = ephemeral; rendezvous wants a fixed port */
  const char *passphrase;       /* NULL/"" = no encryption */
  int pbkeylen;                 /* 0 = library default (16); else 16/24/32 */
  const char *streamid;         /* NULL/"" = none */
  const char *packetfilter;     /* NULL/"" = none; raw SRTO_PACKETFILTER string, e.g. "fec,cols:10,rows:5" */
  unsigned latency_ms;           /* 0 = library default; sets SRTO_LATENCY (both directions) */
  int verbose;                   /* gates libsrt's own NOTICE/DEBUG logging */
  metrics_exporter_t *mx;        /* NULL = no stats push */
  const char *tool_version;      /* required if mx set */
} srtout_cfg_t;

typedef struct srtout srtout_t;

/* NULL on failure. built without libsrt: always fails, logs why */
srtout_t *srtout_open(const srtout_cfg_t *cfg);

/* chunks internally at SRT live-mode payload size. 0 ok, -1 error */
int srtout_write(srtout_t *r, const unsigned char *buf, size_t n);

void srtout_close(srtout_t *r);

#endif
