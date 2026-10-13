/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HLS_INT_H
#define DIPIXY_HLS_INT_H

#include "../segstore_int.h"

typedef struct {
  uint32_t seq;
  double duration;
  uint8_t disc;
  int part_count;
  double part_duration[HLS_MAX_PARTS];
  uint8_t part_independent[HLS_MAX_PARTS];
} ll_seg_snap_t;

/* index_ll.m3u8 fields, copied under store_lock. formatting runs unlocked */
typedef struct {
  int td;
  double part_target;
  uint32_t oldest_seq;
  uint32_t disc_seq; /* breaks no longer listed */
  char lcevc[16]; /* ?lcevc= value for part/segment URIs, empty: default */
  int hb_ms;
  int seg_count;
  ll_seg_snap_t segs[HLS_MAX_SEGS];
  int live_count;
  double live_duration[HLS_MAX_PARTS];
  uint8_t live_independent[HLS_MAX_PARTS];
  uint8_t live_disc;
  uint32_t live_msn;
} ll_playlist_snap_t;

typedef struct {
  uint32_t seq;
  double duration;
  uint8_t disc;
} plain_seg_snap_t;

typedef struct {
  seg_container_t container;
  codec_t video_codec;
  int td;
  uint32_t oldest_seq;
  uint32_t disc_seq; /* breaks no longer listed */
  int init_gen; /* EXT-X-MAP URI changes with each init segment */
  char lcevc[16]; /* ?lcevc= value for segment/init URIs, empty: default */
  int seg_count;
  plain_seg_snap_t segs[HLS_MAX_SEGS];
} plain_playlist_snap_t;

/* alt_pid[i] == 0: base. else: base lcevc_pid[] */
typedef struct {
  int alt_count;
  unsigned alt_pid[PSI_LCEVC_MAX_LINKS + 1];
  uint64_t bandwidth;
} lcevc_master_snap_t;

/* hls_serve.c */
int parse_part_filename(const char *fn, uint32_t *seq, int *part);
void snapshot_plain_playlist(const hls_snapshot_t *s, seg_container_t container, const lcevc_select_t *lcevc, plain_playlist_snap_t *snap);
char *write_lcevc_arg(char *mp, const char *lcevc, char sep);
size_t format_plain_playlist(const plain_playlist_snap_t *snap, char *m3u8, size_t cap);
void snapshot_lcevc_master(const hls_snapshot_t *s, lcevc_master_snap_t *snap);
size_t format_lcevc_master_playlist(const lcevc_master_snap_t *snap, char *m3u8, size_t cap);

/* hls_llhls.c */
void snapshot_ll_playlist(const hls_snapshot_t *s, const lcevc_select_t *lcevc, ll_playlist_snap_t *snap);
size_t format_ll_playlist(const ll_playlist_snap_t *snap, char *m3u8, size_t cap);

typedef enum { HLS_RESOLVE_PLAYLIST, HLS_RESOLVE_INIT, HLS_RESOLVE_SEGMENT } hls_resolve_kind_t;

typedef struct {
  hls_resolve_kind_t kind;
  int status; /* 200/304/404 */
  const char *content_type;
  char etag[48];
  uint8_t *body;
  size_t body_len;
} hls_resolve_t;

/* raw If-None-Match value vs bare etag: "*", or list of [W/]"etag". weak compare */
int hls_inm_match(const char *inm, const char *etag);

int hls_resolve(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container,
                const char *filename, const char *if_none_match, hls_resolve_t *r);

typedef struct {
  hls_resolve_kind_t kind;
  int status;
  const char *content_type;
  char etag[48];
  const uint8_t *body;
  uint8_t *owner; /* seg_buf base of body, NULL for playlists */
  size_t body_len;
} hls_ll_resolve_t;

int hls_resolve_ll(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc,
                   const char *filename, const char *if_none_match, hls_ll_resolve_t *r);

#endif
