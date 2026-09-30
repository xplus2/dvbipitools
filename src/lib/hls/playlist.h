/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HLS_PLAYLIST_H
#define DVBIPITOOLS_LIB_HLS_PLAYLIST_H

#include <stddef.h>

#include "lib/net/httpclient/httpclient.h"

#define HLS_MAX_SEGMENTS 64
#define HLS_MAX_VARIANTS 32

typedef struct {
  char url[2048];
} hls_segment_t;

typedef struct {
  unsigned target_duration;
  unsigned long long media_sequence;
  hls_segment_t segments[HLS_MAX_SEGMENTS];
  unsigned n_segments;
  int endlist;
  int low_latency;
  char map_uri[2048];
} hls_playlist_t;

int hls_playlist_parse(char *body, const http_url_t *base, hls_playlist_t *out);

typedef struct {
  char url[2048];
  unsigned bandwidth;
  unsigned width;
  unsigned height;
  char audio_group_id[64];
} hls_variant_t;

typedef struct {
  char group_id[64];
  char url[2048];
} hls_audio_rendition_t;

typedef struct {
  hls_variant_t variants[HLS_MAX_VARIANTS];
  unsigned n_variants;
  hls_audio_rendition_t audio_renditions[HLS_MAX_VARIANTS];
  unsigned n_audio_renditions;
} hls_master_t;

int hls_body_is_master(const char *body);

int hls_master_parse(char *body, const http_url_t *base, hls_master_t *out);

int hls_master_pick_highest(const hls_master_t *m);

const hls_audio_rendition_t *hls_master_find_audio(const hls_master_t *m, const char *group_id);

#endif
