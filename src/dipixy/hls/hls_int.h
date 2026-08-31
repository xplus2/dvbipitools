/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HLS_INT_H
#define DIPIXY_HLS_INT_H

#include <pthread.h>
#include <stdint.h>
#include <time.h>

#include "hls.h"

typedef struct {
  size_t offset[HLS_MAX_PARTS]; /* offsets index owning data: hls_seg_t.data or store's live_data */
  size_t size[HLS_MAX_PARTS];
  double duration[HLS_MAX_PARTS];
  uint8_t independent[HLS_MAX_PARTS];
  int count;
} hls_parts_t;

typedef struct {
  uint8_t *data;
  size_t size;
  double duration;
  uint32_t seq;
  uint64_t start_ms; /* cumulative offset since store open, DASH $Time$ addressing */
  hls_parts_t parts; /* count 0 if this store never had LL-HLS enabled */
} hls_seg_t;

/* ftyp+moov worst case: FMP4_MAX_TRACKS(4) x FMP4_CPRIV_MAX(512) plus box overhead */
#define HLS_INIT_SEG_MAX 8192

typedef struct {
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid; /* 0 = auto */
  int open;
  double seg_target;
  double td_hw; /* target duration high-water mark, monotonic */
  int max_segs;
  int video_codec; /* 0=AVC, 1=HEVC, drives HLS VERSION */
  hls_container_t container;
  uint8_t init_data[HLS_INIT_SEG_MAX]; /* HLS_CONTAINER_FMP4 only */
  size_t init_size;
  int init_gen; /* bumped each hls_set_init_segment(), part of init.mp4's ETag */
  time_t opened_at; /* MPD availabilityStartTime */
  hls_seg_t segs[HLS_MAX_SEGS];
  int head; /* index of oldest segment in ring */
  int count;
  uint32_t oldest_seq;
  uint32_t next_seq;
  uint64_t cum_ms; /* next segment's start_ms, never reset by eviction */

  double part_target; /* 0 = LL-HLS disabled */
  uint8_t *live_data;  /* in-progress segment bytes, hls_push_part()-accumulated */
  size_t live_len, live_cap;
  hls_parts_t live_parts;
  uint32_t live_msn; /* == next_seq while this segment is in progress */
} hls_store_t;

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} strbuf_t;

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

/* hls.c: buffer pool + store registry */
pthread_mutex_t *store_lock(const hls_store_t *s);
hls_store_t *find_store_locked(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container);
int hls_target_duration(const hls_store_t *s);
uint8_t *seg_buf_alloc(size_t size);
void seg_buf_ref(uint8_t *data);
void seg_buf_unref(uint8_t *data);
void seg_buf_release_cb(void *arg);

/* hls_fmt.c: response formatting primitives, shared by serve/llhls/dash/render */
void hls_sb_init(strbuf_t *b, char *buf, size_t cap);
void hls_sb_add(strbuf_t *b, const char *s);
void hls_sb_add_hex2(strbuf_t *b, unsigned v);
void hls_sb_add_u64(strbuf_t *b, uint64_t v);
void queue_status(conn_t *c, const char *status, int keep_alive);
void queue_not_modified(conn_t *c, const char *etag, int keep_alive);
void cors_prepare(const char *origin_hdr, char *out, size_t outsz);
void queue_m3u8(conn_t *c, const char *body, size_t body_len, int is_head, int keep_alive, const char *cors_hdr);
void queue_segment(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type,
                    const char *etag, int is_head, int keep_alive, const char *cors_hdr);
#define HLS_ZC_MIN_LEN (32u * 1024u)
int hls_zc_eligible(const conn_t *c, size_t body_len, int is_head);
void queue_segment_zc(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type,
                       const char *etag, int keep_alive, const char *cors_hdr);
void seg_etag(uint32_t seq, size_t size, char *out, size_t outsz);
void part_etag(uint32_t seq, int part, size_t size, char *out, size_t outsz);
void init_etag(int gen, size_t size, char *out, size_t outsz);
const char *hls_filename_ext(const char *fn);
void queue_mpd(conn_t *c, const char *body, size_t body_len, int is_head, int keep_alive, const char *cors_hdr);

char *write_lit(char *dst, const char *lit, size_t len);
#define WRITE_LIT(dst, lit) write_lit((dst), (lit), sizeof(lit) - 1)
char *write_u32(char *dst, uint32_t v, int min_digits);
char *write_u64_gen(char *dst, uint64_t v, int min_digits);
char *write_fixed1(char *dst, double v);
char *write_fixed3(char *dst, double v);

/* hls_serve.c */
int parse_part_filename(const char *fn, uint32_t *seq, int *part);

/* hls_llhls.c */
void snapshot_ll_playlist(const hls_store_t *s, ll_playlist_snap_t *snap); /* caller holds store_lock(s) */
size_t format_ll_playlist(const ll_playlist_snap_t *snap, char *m3u8, size_t cap);

/* hls_dash.c */
size_t build_mpd(const hls_store_t *s, char *mpd, size_t cap); /* caller holds store's lock */
int parse_dash_seg_filename(const char *fn, uint64_t *t);
const hls_seg_t *find_seg_by_time(const hls_store_t *s, uint64_t t_ms); /* caller holds store's lock */

#endif
