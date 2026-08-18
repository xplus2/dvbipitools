/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_NET_DVBSTP_SEEN_H
#define LIB_NET_DVBSTP_SEEN_H

#include "dvbstp.h"

#define LISTEN_SEEN_MAX 16

typedef struct {
  unsigned payload_id, segment_id, version;
} seen_t;

/* 1 if h already appears in seen[0..*count), else records it (up to LISTEN_SEEN_MAX) and returns 0 */
static inline int already_seen(seen_t *seen, int *count, const dvbstp_header_t *h) {
  int i;
  for (i = 0; i < *count; i++)
    /* cppcheck-suppress uninitvar -- seen[i] for i<count always written by an earlier call */
    if (seen[i].payload_id == h->payload_id && seen[i].segment_id == h->segment_id && seen[i].version == h->segment_version)
      return 1;
  if (*count < LISTEN_SEEN_MAX) {
    seen[*count].payload_id = h->payload_id;
    seen[*count].segment_id = h->segment_id;
    seen[*count].version = h->segment_version;
    (*count)++;
  }
  return 0;
}

#endif
