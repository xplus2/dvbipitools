/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include <pthread.h>
#include <string.h>

/* caller holds store_lock(s) */
void snapshot_ll_playlist(const hls_store_t *s, ll_playlist_snap_t *snap) {
  int i;
  snap->td = hls_target_duration(s);
  snap->part_target = s->part_target;
  snap->oldest_seq = s->oldest_seq;
  snap->hb_ms = (int)(s->part_target * 3000.0);
  snap->seg_count = s->count;
  for (i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    ll_seg_snap_t *ss = &snap->segs[i];
    int p;
    ss->seq = seg->seq;
    ss->duration = seg->duration;
    ss->part_count = seg->parts.count;
    for (p = 0; p < seg->parts.count; p++) {
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
  if (independent)
    mp = WRITE_LIT(mp, ",INDEPENDENT=YES");
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
  char *end = m3u8 + cap;
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
    int p;
    for (p = 0; p < ss->part_count; p++) {
      if ((size_t)(end - mp) < 128)
        goto done;
      mp = write_part_line(mp, ss->part_duration[p], ss->seq, p, ss->part_independent[p]);
    }
    if ((size_t)(end - mp) < 64)
      goto done;
    mp = write_extinf_line(mp, ss->duration, ss->seq);
  }
  for (i = 0; i < snap->live_count; i++) {
    if ((size_t)(end - mp) < 128)
      goto done;
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

int hls_serve_ll(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *if_none_match, const char *origin_hdr, size_t *out_bytes) {
  hls_store_t *s;
  uint32_t req_seq;
  int req_part;
  char etag[48];
  char cors_hdr[192];
  uint8_t *body = NULL;
  size_t body_len = 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  if (!strcmp(filename, "index_ll.m3u8")) {
    char m3u8[16384];
    ll_playlist_snap_t snap;
    s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_TS);
    if (!s || s->part_target <= 0.0 || (s->count == 0 && s->live_parts.count == 0)) {
      if (s) pthread_mutex_unlock(store_lock(s));
      queue_status(c, "404 Not Found", keep_alive);
      return 1;
    }
    snapshot_ll_playlist(s, &snap);
    pthread_mutex_unlock(store_lock(s));
    {
      size_t m3u8_len = format_ll_playlist(&snap, m3u8, sizeof m3u8);
      queue_m3u8(c, m3u8, m3u8_len, is_head, keep_alive, cors_hdr);
      if (out_bytes)
        *out_bytes = m3u8_len;
    }
    return 1;
  }

  if (!parse_part_filename(filename, &req_seq, &req_part))
    return 0;

  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_TS);
  if (s && s->live_msn == req_seq && req_part < s->live_parts.count) {
    body = s->live_data + s->live_parts.offset[req_part];
    body_len = s->live_parts.size[req_part];
    part_etag(req_seq, req_part, body_len, etag, sizeof etag);
  } else if (s && s->count > 0) {
    uint32_t oldest = s->oldest_seq, last = oldest + (uint32_t)s->count - 1u;
    if (req_seq >= oldest && req_seq <= last) {
      const hls_seg_t *seg = &s->segs[(s->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      if (req_part < seg->parts.count) {
        body = seg->data + seg->parts.offset[req_part];
        body_len = seg->parts.size[req_part];
        part_etag(req_seq, req_part, body_len, etag, sizeof etag);
      }
    }
  }
  if (!body) {
    if (s) pthread_mutex_unlock(store_lock(s));
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  if (if_none_match && !strcmp(if_none_match, etag)) {
    queue_not_modified(c, etag, keep_alive);
  } else { /* conn_queue() copies before returning, safe to unlock right after */
    queue_segment(c, body, body_len, "video/mp2t", etag, is_head, keep_alive, cors_hdr);
    if (out_bytes) *out_bytes = body_len;
  }
  pthread_mutex_unlock(store_lock(s));
  return 1;
}
