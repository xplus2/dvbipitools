/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RISTOUT_H
#define DVBIPITOOLS_LIB_NET_RISTOUT_H

#include <stddef.h>

#include "lib/metrics/export.h"

#define RISTOUT_MAX_PEERS 8

typedef enum { RISTOUT_PROFILE_SIMPLE, RISTOUT_PROFILE_MAIN } ristout_profile_t;

typedef struct {
  const char *peer_uri[RISTOUT_MAX_PEERS]; /* rist://..., bonded onto one context */
  int npeers;
  ristout_profile_t profile;
  const char *secret;      /* NULL/"" = none; profile main only */
  const char *cname;       /* NULL/"" = library default */
  unsigned buffer_ms;      /* recovery_length_min/max on every peer; 0 = library default */
  int verbose;             /* gates librist's own INFO/DEBUG logging */
  metrics_exporter_t *mx;  /* NULL = no stats push */
  const char *tool_version; /* required if mx set */
} ristout_cfg_t;

typedef struct ristout ristout_t;

/* NULL on failure. built without librist: always fails, logs why */
ristout_t *ristout_open(const ristout_cfg_t *cfg);

/* chunks internally at librist's safe payload size. 0 ok, -1 error */
int ristout_write(ristout_t *r, const unsigned char *buf, size_t n);

void ristout_close(ristout_t *r);

#endif
