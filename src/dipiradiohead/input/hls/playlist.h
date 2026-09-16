/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_HLS_PLAYLIST_H
#define DIPIRADIOHEAD_INPUT_HLS_PLAYLIST_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

#define HLS_MAX_SEGMENTS 64

typedef struct {
  char url[2048];
} hls_segment_t;

typedef struct {
  unsigned target_duration;
  unsigned long long media_sequence;
  hls_segment_t segments[HLS_MAX_SEGMENTS];
  unsigned n_segments;
  int endlist;
} hls_playlist_t;

/* body: mutable, NUL terminated (playlist_next_line() writes).
   base: resolves relative segment URIs. 1 OK, 0 not #EXTM3U body */
int hls_playlist_parse(char *body, const http_url_t *base, hls_playlist_t *out);

#endif
