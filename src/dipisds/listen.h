/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_LISTEN_H
#define DIPISDS_LISTEN_H

#include "lib/net/dvbstp.h"

#include "args.h"

int listen_run(const config_t *cfg);

#define LISTEN_SEEN_MAX 16

typedef struct {
  unsigned payload_id, segment_id, version;
} seen_t;

/* 1 if h already appears in seen[0..*count), else records it (up to LISTEN_SEEN_MAX) and returns 0 */
int already_seen(seen_t *seen, int *count, const dvbstp_header_t *h);

#endif
