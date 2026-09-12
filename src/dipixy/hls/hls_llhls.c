/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include <pthread.h>
#include <string.h>

void snapshot_ll_playlist(const hls_snapshot_t *s, ll_playlist_snap_t *snap) {
  int i;
  snap->td = hls_target_duration(s);
  snap->part_target = s->part_target;
  snap->oldest_seq = s->oldest_seq;
  snap->hb_ms = (int)(s->part_target * 3000.0);
  snap->seg_count = s->count;
  for (i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    ll_seg_snap_t *ss = &snap->segs[i];
    ss->seq = seg->seq;
    ss->duration = seg->duration;
    ss->part_count = seg->parts.count;
    for (int p = 0; p < seg->parts.count; p++) {
      ss->part_duration[p] = seg->parts.duration[p];
      ss->part_independent[p] = seg->parts.independent[p];
    }
  }
  snap->live_count = s->live_parts.count;
  for (i = 0; i < s->live_parts.count; i++) {
    snap->live_duration[i] = s->live_parts.duration[i];
    snap->live_independent[i] = s->live_parts.independent[i];
  }
  snap->live_msn = s->live_msn;
}

static char *write_part_line(char *mp, double duration, uint32_t seq, int part, int independent) {
  mp = WRITE_LIT(mp, "#EXT-X-PART:DURATION=");
  mp = write_fixed3(mp, duration);
  mp = WRITE_LIT(mp, ",URI=\"seg");
  mp = write_u32(mp, seq, 5);
  *mp++ = '.';
  mp = write_u32(mp, (uint32_t)part, 0);
  mp = WRITE_LIT(mp, ".ts\"");
  if (independent) mp = WRITE_LIT(mp, ",INDEPENDENT=YES");
  *mp++ = '\n';
  return mp;
}

static char *write_extinf_line(char *mp, double duration, uint32_t seq) {
  mp = WRITE_LIT(mp, "#EXTINF:");
  mp = write_fixed3(mp, duration);
  mp = WRITE_LIT(mp, ",\nseg");
  mp = write_u32(mp, seq, 5);
  return WRITE_LIT(mp, ".ts\n");
}

/* no lock held. returns bytes written */
size_t format_ll_playlist(const ll_playlist_snap_t *snap, char *m3u8, size_t cap) {
  char *mp = m3u8;
  const char *end = m3u8 + cap;
  int i;

  mp = WRITE_LIT(mp, "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-VERSION:6\n#EXT-X-TARGETDURATION:");
  mp = write_u32(mp, (uint32_t)snap->td, 0);
  mp = WRITE_LIT(mp, "\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES,PART-HOLD-BACK=");
  mp = write_fixed3(mp, snap->hb_ms / 1000.0);
  mp = WRITE_LIT(mp, "\n#EXT-X-PART-INF:PART-TARGET=");
  mp = write_fixed3(mp, snap->part_target);
  mp = WRITE_LIT(mp, "\n#EXT-X-MEDIA-SEQUENCE:");
  mp = write_u32(mp, snap->oldest_seq, 0);
  *mp++ = '\n';

  for (i = 0; i < snap->seg_count; i++) {
    const ll_seg_snap_t *ss = &snap->segs[i];
    for (int p = 0; p < ss->part_count; p++) {
      if ((size_t)(end - mp) < 128) goto done;
      mp = write_part_line(mp, ss->part_duration[p], ss->seq, p, ss->part_independent[p]);
    }
    if ((size_t)(end - mp) < 64) goto done;
    mp = write_extinf_line(mp, ss->duration, ss->seq);
  }
  for (i = 0; i < snap->live_count; i++) {
    if ((size_t)(end - mp) < 128) goto done;
    mp = write_part_line(mp, snap->live_duration[i], snap->live_msn, i, snap->live_independent[i]);
  }
  if ((size_t)(end - mp) >= 96) {
    mp = WRITE_LIT(mp, "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"seg");
    mp = write_u32(mp, snap->live_msn, 5);
    *mp++ = '.';
    mp = write_u32(mp, (uint32_t)snap->live_count, 0);
    mp = WRITE_LIT(mp, ".ts\"\n");
  }
done:
  return (size_t)(mp - m3u8);
}

int hls_serve_ll(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  hls_ll_resolve_t r;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  if (!hls_resolve_ll(ctx, filter, pmt_pid, lcevc, filename, if_none_match, &r)) return 0;
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
  } else {
    queue_segment(c, r.body, r.body_len, r.content_type, r.etag, is_head, keep_alive, cors_hdr);
  }
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}
