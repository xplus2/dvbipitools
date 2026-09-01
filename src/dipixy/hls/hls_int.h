/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HLS_INT_H
#define DIPIXY_HLS_INT_H

#include "../segstore_int.h"

typedef struct {
  uint32_t seq;
  double duration;
  int part_count;
  double part_duration[HLS_MAX_PARTS];
  uint8_t part_independent[HLS_MAX_PARTS];
} ll_seg_snap_t;

/* index_ll.m3u8 fields, copied under store_lock. formatting runs unlocked */
typedef struct {
  int td;
  double part_target;
  uint32_t oldest_seq;
  int hb_ms;
  int seg_count;
  ll_seg_snap_t segs[HLS_MAX_SEGS];
  int live_count;
  double live_duration[HLS_MAX_PARTS];
  uint8_t live_independent[HLS_MAX_PARTS];
  uint32_t live_msn;
} ll_playlist_snap_t;

/* hls_serve.c */
int parse_part_filename(const char *fn, uint32_t *seq, int *part);

/* hls_llhls.c */
void snapshot_ll_playlist(const hls_store_t *s, ll_playlist_snap_t *snap); /* caller holds store_lock(s) */
size_t format_ll_playlist(const ll_playlist_snap_t *snap, char *m3u8, size_t cap);

#endif
