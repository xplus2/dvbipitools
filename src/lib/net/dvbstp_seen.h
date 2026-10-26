/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_DVBSTP_SEEN_H
#define DVBIPITOOLS_LIB_NET_DVBSTP_SEEN_H

#include "dvbstp.h"

#define LISTEN_SEEN_MAX 256

typedef struct {
  dvbstp_key_t key;
  unsigned version;
} dvbstp_seen_t;

/* 1 if h with its version already appears in seen[0..*count), else records it and returns 0.
   new version of a key replaces old one, new keys up to LISTEN_SEEN_MAX */
static inline int dvbstp_already_seen(dvbstp_seen_t *seen, int *count, const dvbstp_header_t *h) {
  int i;
  for (i = 0; i < *count; i++) {
    /* cppcheck-suppress uninitvar -- seen[i] for i<count always written by an earlier call */
    if (!dvbstp_key_match(&seen[i].key, h)) continue;
    if (seen[i].version == h->segment_version) return 1;
    seen[i].version = h->segment_version;
    return 0;
  }
  if (*count < LISTEN_SEEN_MAX) {
    dvbstp_key_set(&seen[*count].key, h);
    seen[*count].version = h->segment_version;
    (*count)++;
  }
  return 0;
}

#endif
