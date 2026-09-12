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

void snapshot_lcevc_master(const hls_snapshot_t *s, lcevc_master_snap_t *snap) {
  uint64_t bw_bits = 0;
  double bw_secs = 0.0;
  snap->alt_count = s->lcevc_pid_count + 1;
  snap->alt_pid[0] = 0;
  for (int i = 0; i < s->lcevc_pid_count; i++) snap->alt_pid[i + 1] = s->lcevc_pid[i];
  for (int i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    bw_bits += (uint64_t)seg->size * 8;
    bw_secs += seg->duration;
  }
  snap->bandwidth = bw_secs > 0.0 ? (uint64_t)(bw_bits / bw_secs) : 1000000ULL;
}

size_t format_lcevc_master_playlist(const lcevc_master_snap_t *snap, char *m3u8, size_t cap) {
  char *mp = m3u8;
  mp = WRITE_LIT(mp, "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n");
  for (int i = 0; i < snap->alt_count; i++) {
    if ((size_t)(mp - m3u8) + 128 > cap) break;
    mp = WRITE_LIT(mp, "#EXT-X-MEDIA:TYPE=VIDEO,GROUP-ID=\"lcevc\",NAME=\"");
    if (snap->alt_pid[i]) {
      mp = WRITE_LIT(mp, "pid ");
      mp = write_u32(mp, snap->alt_pid[i], 0);
    } else {
      mp = WRITE_LIT(mp, "base");
    }
    mp = WRITE_LIT(mp, "\",AUTOSELECT=YES");
    if (i == 0) mp = WRITE_LIT(mp, ",DEFAULT=YES");
    mp = WRITE_LIT(mp, ",URI=\"?lcevc=");
    if (snap->alt_pid[i]) mp = write_u32(mp, snap->alt_pid[i], 0);
    else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\"\n");
  }
  mp = WRITE_LIT(mp, "#EXT-X-STREAM-INF:BANDWIDTH=");
  mp = write_u64_gen(mp, snap->bandwidth, 0);
  mp = WRITE_LIT(mp, ",VIDEO=\"lcevc\"\n?lcevc=base\n");
  return (size_t)(mp - m3u8);
}

int hls_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container,
              const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  hls_resolve_t r;

  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  if (!hls_resolve(ctx, filter, pmt_pid, lcevc, container, filename, if_none_match, &r)) return 0;
  if (r.status == 404) {
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  if (r.status == 304) {
    queue_not_modified(c, r.etag, keep_alive);
    return 1;
  }
  if (r.kind == HLS_RESOLVE_PLAYLIST) {
    queue_m3u8(c, (const char *)r.body, r.body_len, is_head, keep_alive, cors_hdr);
  } else if (r.kind == HLS_RESOLVE_SEGMENT && hls_zc_eligible(c, r.body_len, is_head)) {
    seg_buf_ref(r.body);
    queue_segment_zc(c, r.body, r.body_len, r.content_type, r.etag, keep_alive, cors_hdr);
  } else {
    queue_segment(c, r.body, r.body_len, r.content_type, r.etag, is_head, keep_alive, cors_hdr);
  }
  if (out_bytes) *out_bytes = r.body_len;
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
