/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTSINK_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTSINK_H

#include <stddef.h>

#include "lib/metrics/export.h"

/* zero-dep wrapper on srtout_t (unlike srtout.h, no <srt/srt.h> dependency). */
#define SRTSINK_MAX_PEERS 8

typedef enum { SRTSINK_GROUP_NONE, SRTSINK_GROUP_BROADCAST, SRTSINK_GROUP_BACKUP } srtsink_group_mode_t;

typedef struct {
  const char *host;
  unsigned port;
} srtsink_peer_t;

typedef struct {
  srtsink_peer_t peers[SRTSINK_MAX_PEERS]; /* npeers > 1: bonded onto one group */
  int npeers;
  srtsink_group_mode_t group_mode; /* NONE: plain connect/rendezvous to peers[0] */
  const char *passphrase;          /* NULL/"" = no encryption */
  int pbkeylen;                    /* 0 = default (16), else 16/24/32 */
  const char *streamid;            /* NULL/"" = none */
  const char *packetfilter;        /* NULL/"" = none, raw SRTO_PACKETFILTER string */
  unsigned latency_ms;              /* 0 = library default */
  int verbose;                      /* gates libsrt's own NOTICE/DEBUG logging */
  metrics_exporter_t *mx;           /* NULL = no stats push */
  const char *tool_version;         /* required if mx set */
  unsigned safety_mult;             /* pending-queue latency-window multiplier, 0 = default 4, clamped to 32 */
} srtsink_cfg_t;

typedef struct srtsink srtsink_t;

typedef struct {
  int connected; /* 1 once link established, writable */
} srtsink_status_t;

/* NULL: bad config or setup failure. connect is async, writes queue till
   link is up. no libsrt: always fails */
srtsink_t *srtsink_open(const srtsink_cfg_t *cfg);

/* non-blocking: advances connect/reconnect, flushes queue once writable.
   call every loop iteration */
void srtsink_service(srtsink_t *r, srtsink_status_t *out);

/* never blocks: chunks to SRT payload size, queues unsent data (bounded),
   drops oldest on overflow */
void srtsink_write(srtsink_t *r, const unsigned char *buf, size_t n);

void srtsink_close(srtsink_t *r);

#endif
