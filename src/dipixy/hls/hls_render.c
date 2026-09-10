/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include "lib/helper/ioutil.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

int hls_render(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_store_t *s;
  hls_snapshot_t *snap;
  char m3u8[4096];
  char etag[48];
  int m3u8_len;
  const char *ext;
  unsigned long seq_ul;
  uint32_t req_seq, oldest, last;
  uint8_t *body = NULL;
  size_t body_len = 0;
  memset(out, 0, sizeof *out);
  ext = hls_filename_ext(filename);
  if (!ext) return 0;
  s = find_store(ctx, filter, pmt_pid, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;

  if (!strcmp(ext, "index")) {
    plain_playlist_snap_t psnap;
    if (!snap || snap->count == 0) {
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    snapshot_plain_playlist(snap, container, &psnap);
    m3u8_len = (int)format_plain_playlist(&psnap, m3u8, sizeof m3u8);
    resp_set(out, 200, "application/vnd.apple.mpegurl", NULL, (uint8_t *)m3u8, (size_t)m3u8_len, is_head);
    return 1;
  }
  if (!strcmp(ext, "init")) {
    if (snap && snap->init_size) {
      body = snap->init_data;
      body_len = snap->init_size;
      init_etag(snap->init_gen, snap->init_size, etag, sizeof etag);
    }
    if (!body) {
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    if (if_none_match && !strcmp(if_none_match, etag)) {
      resp_set(out, 304, NULL, etag, NULL, 0, is_head);
    } else {
      resp_set(out, 200, "video/mp4", etag, body, body_len, is_head);
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
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  if (if_none_match && !strcmp(if_none_match, etag)) {
    resp_set(out, 304, NULL, etag, NULL, 0, is_head);
  } else {
    resp_set_zc(out, 200, !strcmp(ext, "m4s") ? "video/mp4" : "video/mp2t", etag, body, body_len, is_head);
  }
  return 1;
}

int hls_render_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_store_t *s;
  hls_snapshot_t *snap;
  uint32_t req_seq;
  int req_part;
  char etag[48];
  const uint8_t *body = NULL;
  size_t body_len = 0;
  memset(out, 0, sizeof *out);
  s = find_store(ctx, filter, pmt_pid, SEG_CONTAINER_TS);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;

  if (!strcmp(filename, "index_ll.m3u8")) {
    char m3u8[16384];
    ll_playlist_snap_t llsnap;
    if (!snap || snap->part_target <= 0.0 || (snap->count == 0 && snap->live_parts.count == 0)) {
      resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
      return 1;
    }
    snapshot_ll_playlist(snap, &llsnap);
    {
      size_t m3u8_len = format_ll_playlist(&llsnap, m3u8, sizeof m3u8);
      resp_set(out, 200, "application/vnd.apple.mpegurl", NULL, (uint8_t *)m3u8, m3u8_len, is_head);
    }
    return 1;
  }

  if (!parse_part_filename(filename, &req_seq, &req_part)) return 0;

  if (snap && snap->live_msn == req_seq && req_part < snap->live_parts.count) {
    body = snap->live_data + snap->live_parts.offset[req_part];
    body_len = snap->live_parts.size[req_part];
    part_etag(req_seq, req_part, body_len, etag, sizeof etag);
  } else if (snap && snap->count > 0) {
    uint32_t oldest = snap->oldest_seq, last = oldest + (uint32_t)snap->count - 1u;
    if (req_seq >= oldest && req_seq <= last) {
      const hls_seg_t *seg = &snap->segs[(snap->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      if (req_part < seg->parts.count) {
        body = seg->data + seg->parts.offset[req_part];
        body_len = seg->parts.size[req_part];
        part_etag(req_seq, req_part, body_len, etag, sizeof etag);
      }
    }
  }
  if (!body) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  if (if_none_match && !strcmp(if_none_match, etag)) {
    resp_set(out, 304, NULL, etag, NULL, 0, is_head);
  } else {
    resp_set(out, 200, "video/mp2t", etag, body, body_len, is_head);
  }
  return 1;
}
