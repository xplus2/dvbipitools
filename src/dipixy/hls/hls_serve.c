/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

int hls_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container,
              const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes) {
  hls_store_t *s;
  char m3u8[4096];
  char *mp;
  char etag[48];
  char cors_hdr[192];
  int m3u8_len, td;
  const char *ext;
  unsigned long seq_ul;
  uint32_t req_seq, oldest, last;
  uint8_t *body = NULL;
  size_t body_len = 0;

  ext = hls_filename_ext(filename);
  if (!ext)
    return 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);

  if (!strcmp(ext, "index")) {
    const char *seg_ext;
    s = find_store_locked(ctx, filter, pmt_pid, container);
    if (!s || s->count == 0) {
      if (s) pthread_mutex_unlock(store_lock(s));
      queue_status(c, "404 Not Found", keep_alive);
      return 1;
    }
    seg_ext = s->container == SEG_CONTAINER_FMP4 ? "m4s" : "ts";
    td = hls_target_duration(s);
    mp = m3u8;
    mp = WRITE_LIT(mp, "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-VERSION:");
    mp = write_u32(mp, s->container == SEG_CONTAINER_FMP4 || s->video_codec == 1 ? 7u : 3u, 0);
    mp = WRITE_LIT(mp, "\n#EXT-X-TARGETDURATION:");
    mp = write_u32(mp, (uint32_t)td, 0);
    mp = WRITE_LIT(mp, "\n#EXT-X-MEDIA-SEQUENCE:");
    mp = write_u32(mp, s->oldest_seq, 0);
    *mp++ = '\n';
    if (s->container == SEG_CONTAINER_FMP4) mp = WRITE_LIT(mp, "#EXT-X-MAP:URI=\"init.mp4\"\n");
    for (int i = 0; i < s->count; i++) {
      const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
      if ((size_t)(mp - m3u8) + 64 > sizeof m3u8)
        break;
      mp = WRITE_LIT(mp, "#EXTINF:");
      mp = write_fixed3(mp, seg->duration);
      mp = WRITE_LIT(mp, ",\nseg");
      mp = write_u32(mp, seg->seq, 5);
      *mp++ = '.';
      mp = write_lit(mp, seg_ext, strlen(seg_ext));
      *mp++ = '\n';
    }
    m3u8_len = (int)(mp - m3u8);
    pthread_mutex_unlock(store_lock(s));
    queue_m3u8(c, m3u8, (size_t)m3u8_len, is_head, keep_alive, cors_hdr);
    if (out_bytes) *out_bytes = (size_t)m3u8_len;
    return 1;
  }

  if (!strcmp(ext, "init")) {
    s = find_store_locked(ctx, filter, pmt_pid, container);
    if (s && s->init_size) {
      body = s->init_data;
      body_len = s->init_size;
      init_etag(s->init_gen, s->init_size, etag, sizeof etag);
    }
    if (!body) {
      if (s) pthread_mutex_unlock(store_lock(s));
      queue_status(c, "404 Not Found", keep_alive);
      return 1;
    }
    if (if_none_match && !strcmp(if_none_match, etag)) {
      queue_not_modified(c, etag, keep_alive);
    } else {
      queue_segment(c, body, body_len, "video/mp4", etag, is_head, keep_alive, cors_hdr);
      if (out_bytes) *out_bytes = body_len;
    }
    pthread_mutex_unlock(store_lock(s));
    return 1;
  }

  seq_ul = strtoul(filename + 3, NULL, 10);
  req_seq = (uint32_t)seq_ul;
  s = find_store_locked(ctx, filter, pmt_pid, container);
  if (s && s->count > 0) {
    oldest = s->oldest_seq;
    last = oldest + (uint32_t)s->count - 1u;
    if (req_seq >= oldest && req_seq <= last) {
      hls_seg_t *seg = &s->segs[(s->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      body = seg->data;
      body_len = seg->size;
      seg_etag(seg->seq, seg->size, etag, sizeof etag);
    }
  }
  if (!body) {
    if (s) pthread_mutex_unlock(store_lock(s));
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  /* queue_segment() copies before unlock, queue_segment_zc() holds a ref already: both safe here */
  if (if_none_match && !strcmp(if_none_match, etag)) {
    queue_not_modified(c, etag, keep_alive);
  } else {
    if (hls_zc_eligible(c, body_len, is_head)) {
      seg_buf_ref(body);
      queue_segment_zc(c, body, body_len, !strcmp(ext, "m4s") ? "video/mp4" : "video/mp2t", etag, keep_alive, cors_hdr);
    } else {
      queue_segment(c, body, body_len, !strcmp(ext, "m4s") ? "video/mp4" : "video/mp2t", etag, is_head, keep_alive, cors_hdr);
    }
    if (out_bytes) *out_bytes = body_len;
  }
  pthread_mutex_unlock(store_lock(s));
  return 1;
}

/* parses "segNNNNN.PP.ts". 1 ok (seq/part set), 0 wrong shape */
int parse_part_filename(const char *fn, uint32_t *seq, int *part) {
  const char *p;
  char *end;
  unsigned long v;

  if (strncmp(fn, "seg", 3) != 0) return 0;
  p = fn + 3;
  if (*p < '0' || *p > '9') return 0;
  v = strtoul(p, &end, 10);
  if (*end != '.') return 0;
  *seq = (uint32_t)v;
  p = end + 1;
  if (*p < '0' || *p > '9') return 0;
  v = strtoul(p, &end, 10);
  if (strcmp(end, ".ts") != 0) return 0;
  *part = (int)v;
  return 1;
}
