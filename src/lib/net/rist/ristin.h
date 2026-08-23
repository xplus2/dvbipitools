/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTIN_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTIN_H

#include "lib/metrics/export.h"

typedef enum { RISTIN_PROFILE_SIMPLE, RISTIN_PROFILE_MAIN } ristin_profile_t;

typedef struct {
  const char *peer_uri;    /* rist://@host:port[?query], single peer, @ required (listen) */
  ristin_profile_t profile;
  const char *secret;      /* NULL/"" = none; profile main only */
  const char *cname;       /* NULL/"" = library default */
  unsigned buffer_ms;      /* recovery_length_min/max; 0 = library default */
  int verbose;             /* gates librist's own INFO/DEBUG logging */
  metrics_exporter_t *mx;  /* NULL = no stats push */
  const char *tool_version; /* required if mx set */
} ristin_cfg_t;

typedef struct ristin ristin_t;

/* NULL on failure (bad/non-@ uri, librist error, pipe/thread setup). w/o librist: always fail */
ristin_t *ristin_open(const ristin_cfg_t *cfg);

/* blocking, poll-safe read end of internal pipe. TS payload bytes, no RTP. valid for r's lifetime */
int ristin_fd(const ristin_t *r);

/* stops reader thread, joins, rist_destroy(), closes pipe */
void ristin_close(ristin_t *r);

#endif
