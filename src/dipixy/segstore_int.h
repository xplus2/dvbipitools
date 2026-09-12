/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_SEGSTORE_INT_H
#define DIPIXY_SEGSTORE_INT_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "segstore.h"
#include "ts/lcevcselect.h"
#include "reactor/qsbr.h"

typedef struct {
  char *text;
  size_t len;
} cached_text_t;

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

typedef struct hls_snapshot {
  hls_seg_t segs[HLS_MAX_SEGS];
  int head; /* index of oldest segment in ring */
  int count;
  uint32_t oldest_seq;
  uint32_t next_seq;
  uint64_t cum_ms; /* next segment's start_ms, never reset by eviction */
  double td_hw; /* target dur high water mark */

  uint8_t *init_data; /* SEG_CONTAINER_FMP4 only, seg_buf_alloc'd */
  size_t init_size;
  int init_gen; /* bump on hls_set_init_segment(), init.mp4 ETag component */
  codec_t video_codec; /* HLS VERSION + DASH codecs= */
  char vcodec_str[32];
  char acodec_str[32];

  double part_target; /* 0 = LL disabled */
  uint8_t *live_data;  /* in-progress segment bytes, hls_push_part()-accumulated */
  size_t live_len, live_cap;
  hls_parts_t live_parts;
  uint32_t live_msn; /* == next_seq while this segment is in progress */

  unsigned lcevc_pid[PSI_LCEVC_MAX_LINKS]; /* base video ES's own paired PIDs */
  int lcevc_pid_count; /* 0: no LCEVC, or not known (yet) */

  _Atomic(cached_text_t *) cache_plain;
  _Atomic(cached_text_t *) cache_ll;
  _Atomic(cached_text_t *) cache_lcevc[2];
  _Atomic(cached_text_t *) cache_mpd;
  _Atomic(cached_text_t *) cache_mpd_ll;
} hls_snapshot_t;

#define HLS_SNAP_RETIRE_DEPTH 4

typedef struct hls_store_t {
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid; /* 0 = auto */
  lcevc_select_t lcevc;
  double seg_target;
  int max_segs;
  seg_container_t container;
  time_t opened_at; /* MPD availabilityStartTime */
  _Atomic int lldash_sub_head;
  _Atomic(hls_snapshot_t *) snap; /* published; NULL till 1st push */

  hls_snapshot_t *retiring[HLS_SNAP_RETIRE_DEPTH];
  uint64_t retiring_mark[HLS_SNAP_RETIRE_DEPTH][QSBR_MAX_WORKERS];
  int retiring_n;
} hls_store_t;

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} strbuf_t;

/* store_lock() is writer only */
pthread_mutex_t *store_lock(const hls_store_t *s);
int hls_target_duration(const hls_snapshot_t *snap);
uint8_t *seg_buf_alloc(size_t size);
void seg_buf_ref(uint8_t *data);
void seg_buf_unref(uint8_t *data);
void seg_buf_release_cb(void *arg);

typedef size_t (*text_fmt_fn)(void *ctx, char *buf, size_t cap);
const cached_text_t *snapshot_cache_text(_Atomic(cached_text_t *) *slot, text_fmt_fn fmt, void *ctx, size_t buf_cap);

/* respfmt.c: response formatting primitives, shared by hls/dash serve/render */
void hls_sb_init(strbuf_t *b, char *buf, size_t cap);
void hls_sb_add(strbuf_t *b, const char *s);
void hls_sb_add_hex2(strbuf_t *b, unsigned v);
void hls_sb_add_u64(strbuf_t *b, uint64_t v);
void queue_status(conn_t *c, const char *status, int keep_alive);
void queue_not_modified(conn_t *c, const char *etag, int keep_alive);
void cors_prepare(const char *origin_hdr, char *out, size_t outsz);
void set_persistence(conn_t *c, int keep_alive);
void queue_m3u8(conn_t *c, const char *body, size_t body_len, int is_head, int keep_alive, const char *cors_hdr);
void queue_segment(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type, const char *etag, int is_head, int keep_alive, const char *cors_hdr);
#define HLS_ZC_MIN_LEN (32u * 1024u)
int hls_zc_eligible(const conn_t *c, size_t body_len, int is_head);
void queue_segment_zc(conn_t *c, const uint8_t *body, size_t body_len, const char *content_type, const char *etag, int keep_alive, const char *cors_hdr);
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

void resp_set(hls_resp_t *out, int status, const char *content_type, const char *etag, const uint8_t *body, size_t body_len, int is_head);
void resp_set_zc(hls_resp_t *out, int status, const char *content_type, const char *etag, uint8_t *body, size_t body_len, int is_head);

#endif
