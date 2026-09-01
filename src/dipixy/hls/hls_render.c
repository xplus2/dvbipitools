/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include "lib/helper/ioutil.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static void resp_set(hls_resp_t *out, int status, const char *content_type, const char *etag, const uint8_t *body, size_t body_len, int is_head) {
  out->status = status;
  out->content_type = content_type;
  if (etag)
    bufcpy(out->etag, sizeof out->etag, etag);
  else
    out->etag[0] = '\0';
  out->body_len = body_len;
  out->body = NULL;
  if (!body || is_head)
    return;
  out->body = malloc(body_len);
  if (!out->body) {
    out->status = 500;
    out->content_type = NULL;
    out->body_len = 0;
    return;
  }
  memcpy(out->body, body, body_len);
}

/* seg_buf-backed body: ref instead of copy. below HLS_ZC_MIN_LEN, falls back to resp_set() */
static void resp_set_zc(hls_resp_t *out, int status, const char *content_type, const char *etag, uint8_t *body, size_t body_len, int is_head) {
  if (!body || is_head || body_len < HLS_ZC_MIN_LEN) {
    resp_set(out, status, content_type, etag, body, body_len, is_head);
    return;
  }
  out->status = status;
  out->content_type = content_type;
  if (etag)
    bufcpy(out->etag, sizeof out->etag, etag);
  else
    out->etag[0] = '\0';
  seg_buf_ref(body);
  out->body = body;
  out->body_len = body_len;
  out->zc = 1;
}

void hls_resp_body_release(uint8_t *body, int zc) {
  if (zc) {
    seg_buf_unref(body);
  } else {
    free(body);
  }
}

int hls_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_store_t *s;
  char m3u8[4096];
  char *mp;
  char etag[48];
  int m3u8_len, td;
  const char *ext;
  unsigned long seq_ul;
  uint32_t req_seq, oldest, last;
  uint8_t *body = NULL;
  size_t body_len = 0;
  memset(out, 0, sizeof *out);
  ext = hls_filename_ext(filename);
  if (!ext) return 0;
  if (!strcmp(ext, "index")) {
    const char *seg_ext;
    s = find_store_locked(ctx, filter, pmt_pid, container);
    if (!s || s->count == 0) {
      if (s) pthread_mutex_unlock(store_lock(s));
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    seg_ext = s->container == HLS_CONTAINER_FMP4 ? "m4s" : "ts";
    td = hls_target_duration(s);
    mp = m3u8;
    mp = WRITE_LIT(mp, "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-VERSION:");
    mp = write_u32(mp, s->container == HLS_CONTAINER_FMP4 || s->video_codec == 1 ? 7u : 3u, 0);
    mp = WRITE_LIT(mp, "\n#EXT-X-TARGETDURATION:");
    mp = write_u32(mp, (uint32_t)td, 0);
    mp = WRITE_LIT(mp, "\n#EXT-X-MEDIA-SEQUENCE:");
    mp = write_u32(mp, s->oldest_seq, 0);
    *mp++ = '\n';
    if (s->container == HLS_CONTAINER_FMP4) mp = WRITE_LIT(mp, "#EXT-X-MAP:URI=\"init.mp4\"\n");
    for (int i = 0; i < s->count; i++) {
      const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
      if ((size_t)(mp - m3u8) + 64 > sizeof m3u8) break;
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
    resp_set(out, 200, "application/vnd.apple.mpegurl", NULL, (uint8_t *)m3u8, (size_t)m3u8_len, is_head);
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
      if (s)
        pthread_mutex_unlock(store_lock(s));
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    if (if_none_match && !strcmp(if_none_match, etag))
      resp_set(out, 304, NULL, etag, NULL, 0, is_head);
    else
      resp_set(out, 200, "video/mp4", etag, body, body_len, is_head);
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
    if (s)
      pthread_mutex_unlock(store_lock(s));
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  if (if_none_match && !strcmp(if_none_match, etag)) {
    resp_set(out, 304, NULL, etag, NULL, 0, is_head);
  } else {
    resp_set_zc(out, 200, !strcmp(ext, "m4s") ? "video/mp4" : "video/mp2t", etag, body, body_len, is_head);
  }
  pthread_mutex_unlock(store_lock(s));
  return 1;
}

int hls_render_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_store_t *s;
  uint32_t req_seq;
  int req_part;
  char etag[48];
  const uint8_t *body = NULL;
  size_t body_len = 0;
  memset(out, 0, sizeof *out);
  if (!strcmp(filename, "index_ll.m3u8")) {
    char m3u8[16384];
    ll_playlist_snap_t snap;
    s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_TS);
    if (!s || s->part_target <= 0.0 || (s->count == 0 && s->live_parts.count == 0)) {
      if (s)
        pthread_mutex_unlock(store_lock(s));
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    snapshot_ll_playlist(s, &snap);
    pthread_mutex_unlock(store_lock(s));
    {
      size_t m3u8_len = format_ll_playlist(&snap, m3u8, sizeof m3u8);
      resp_set(out, 200, "application/vnd.apple.mpegurl", NULL, (uint8_t *)m3u8, m3u8_len, is_head);
    }
    return 1;
  }

  if (!parse_part_filename(filename, &req_seq, &req_part)) return 0;

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
    if (s)
      pthread_mutex_unlock(store_lock(s));
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  if (if_none_match && !strcmp(if_none_match, etag))
    resp_set(out, 304, NULL, etag, NULL, 0, is_head);
  else
    resp_set(out, 200, "video/mp2t", etag, body, body_len, is_head);
  pthread_mutex_unlock(store_lock(s));
  return 1;
}

int hls_render_dash(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int is_head, hls_resp_t *out) {
  const hls_store_t *s;
  char mpd[8192];
  size_t mpd_len;
  memset(out, 0, sizeof *out);
  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_FMP4);
  if (!s || s->count == 0) {
    if (s) pthread_mutex_unlock(store_lock(s));
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  mpd_len = build_mpd(s, mpd, sizeof mpd);
  pthread_mutex_unlock(store_lock(s));
  resp_set(out, 200, "application/dash+xml", NULL, (uint8_t *)mpd, mpd_len, is_head);
  return 1;
}

int hls_render_dash_seg(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, hls_resp_t *out) {
  const hls_store_t *s;
  const hls_seg_t *seg;
  uint64_t req_t;
  char etag[48];
  memset(out, 0, sizeof *out);
  if (!parse_dash_seg_filename(filename, &req_t)) return 0;
  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_FMP4);
  seg = s ? find_seg_by_time(s, req_t) : NULL;
  if (!seg) {
    if (s) pthread_mutex_unlock(store_lock(s));
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  seg_etag(seg->seq, seg->size, etag, sizeof etag);
  resp_set_zc(out, 200, "video/mp4", etag, seg->data, seg->size, is_head);
  pthread_mutex_unlock(store_lock(s));
  return 1;
}
