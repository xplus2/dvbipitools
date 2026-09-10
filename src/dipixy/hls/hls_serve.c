/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

void snapshot_plain_playlist(const hls_snapshot_t *s, seg_container_t container, plain_playlist_snap_t *snap) {
  snap->container = container;
  snap->video_codec = s->video_codec;
  snap->td = hls_target_duration(s);
  snap->oldest_seq = s->oldest_seq;
  snap->seg_count = s->count;
  for (int i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    snap->segs[i].seq = seg->seq;
    snap->segs[i].duration = seg->duration;
  }
}

size_t format_plain_playlist(const plain_playlist_snap_t *snap, char *m3u8, size_t cap) {
  char *mp = m3u8;
  const char *seg_ext = snap->container == SEG_CONTAINER_FMP4 ? "m4s" : "ts";
  mp = WRITE_LIT(mp, "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-VERSION:");
  mp = write_u32(mp, snap->container == SEG_CONTAINER_FMP4 || snap->video_codec == CODEC_HEVC || snap->video_codec == CODEC_VVC ? 7u : 3u, 0);
  mp = WRITE_LIT(mp, "\n#EXT-X-TARGETDURATION:");
  mp = write_u32(mp, (uint32_t)snap->td, 0);
  mp = WRITE_LIT(mp, "\n#EXT-X-MEDIA-SEQUENCE:");
  mp = write_u32(mp, snap->oldest_seq, 0);
  *mp++ = '\n';
  if (snap->container == SEG_CONTAINER_FMP4) mp = WRITE_LIT(mp, "#EXT-X-MAP:URI=\"init.mp4\"\n");
  for (int i = 0; i < snap->seg_count; i++) {
    if ((size_t)(mp - m3u8) + 64 > cap) break;
    mp = WRITE_LIT(mp, "#EXTINF:");
    mp = write_fixed3(mp, snap->segs[i].duration);
    mp = WRITE_LIT(mp, ",\nseg");
    mp = write_u32(mp, snap->segs[i].seq, 5);
    *mp++ = '.';
    mp = write_lit(mp, seg_ext, strlen(seg_ext));
    *mp++ = '\n';
  }
  return (size_t)(mp - m3u8);
}

int hls_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container,
              const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes) {
  hls_store_t *s;
  hls_snapshot_t *snap;
  char m3u8[4096];
  char etag[48];
  char cors_hdr[192];
  int m3u8_len;
  const char *ext;
  unsigned long seq_ul;
  uint32_t req_seq, oldest, last;
  uint8_t *body = NULL;
  size_t body_len = 0;

  ext = hls_filename_ext(filename);
  if (!ext) return 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  s = find_store(ctx, filter, pmt_pid, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;

  if (!strcmp(ext, "index")) {
    plain_playlist_snap_t psnap;
    if (!snap || snap->count == 0) {
      queue_status(c, "404 Not Found", keep_alive);
      return 1;
    }
    snapshot_plain_playlist(snap, container, &psnap);
    m3u8_len = (int)format_plain_playlist(&psnap, m3u8, sizeof m3u8);
    queue_m3u8(c, m3u8, (size_t)m3u8_len, is_head, keep_alive, cors_hdr);
    if (out_bytes) *out_bytes = (size_t)m3u8_len;
    return 1;
  }

  if (!strcmp(ext, "init")) {
    if (snap && snap->init_size) {
      body = snap->init_data;
      body_len = snap->init_size;
      init_etag(snap->init_gen, snap->init_size, etag, sizeof etag);
    }
    if (!body) {
      queue_status(c, "404 Not Found", keep_alive);
      return 1;
    }
    if (if_none_match && !strcmp(if_none_match, etag)) {
      queue_not_modified(c, etag, keep_alive);
    } else {
      queue_segment(c, body, body_len, "video/mp4", etag, is_head, keep_alive, cors_hdr);
      if (out_bytes) *out_bytes = body_len;
    }
    return 1;
  }

  seq_ul = strtoul(filename + 3, NULL, 10);
  req_seq = (uint32_t)seq_ul;
  if (snap && snap->count > 0) {
    oldest = snap->oldest_seq;
    last = oldest + (uint32_t)snap->count - 1u;
    if (req_seq >= oldest && req_seq <= last) {
      const hls_seg_t *seg = &snap->segs[(snap->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      body = seg->data;
      body_len = seg->size;
      seg_etag(seg->seq, seg->size, etag, sizeof etag);
    }
  }
  if (!body) {
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
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
