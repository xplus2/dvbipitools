/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include "lib/helper/ioutil.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static size_t fmt_plain_cb(void *ctx, char *buf, size_t cap) {
  return format_plain_playlist((const plain_playlist_snap_t *)ctx, buf, cap);
}

static size_t fmt_lcevc_cb(void *ctx, char *buf, size_t cap) {
  return format_lcevc_master_playlist((const lcevc_master_snap_t *)ctx, buf, cap);
}

static size_t fmt_ll_cb(void *ctx, char *buf, size_t cap) {
  return format_ll_playlist((const ll_playlist_snap_t *)ctx, buf, cap);
}

#define HLS_PLAYLIST_BUF_CAP 4096

int hls_resolve(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container,
                const char *filename, const char *if_none_match, hls_resolve_t *r) {
  hls_store_t *s;
  hls_snapshot_t *snap;
  const char *ext;
  unsigned long seq_ul;
  uint32_t req_seq, oldest, last;
  memset(r, 0, sizeof *r);
  ext = hls_filename_ext(filename);
  if (!ext) return 0;
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (!strcmp(ext, "index")) {
    r->kind = HLS_RESOLVE_PLAYLIST;
    if (!snap || snap->count == 0) {
      r->status = 404;
      return 1;
    }
    if (lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
      lcevc_master_snap_t msnap;
      const cached_text_t *ct;
      snapshot_lcevc_master(snap, &msnap);
      ct = snapshot_cache_text(&snap->cache_lcevc[0], fmt_lcevc_cb, &msnap, HLS_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (uint8_t *)ct->text;
      r->body_len = ct->len;
    } else {
      plain_playlist_snap_t psnap;
      const cached_text_t *ct;
      snapshot_plain_playlist(snap, container, &psnap);
      ct = snapshot_cache_text(&snap->cache_plain, fmt_plain_cb, &psnap, HLS_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (uint8_t *)ct->text;
      r->body_len = ct->len;
    }
    r->status = 200;
    r->content_type = "application/vnd.apple.mpegurl";
    return 1;
  }

  if (!strcmp(ext, "init")) {
    r->kind = HLS_RESOLVE_INIT;
    if (snap && snap->init_size) {
      r->body = snap->init_data;
      r->body_len = snap->init_size;
      r->content_type = "video/mp4";
      init_etag(snap->init_gen, snap->init_size, r->etag, sizeof r->etag);
    }
  } else {
    r->kind = HLS_RESOLVE_SEGMENT;
    seq_ul = strtoul(filename + 3, NULL, 10);
    req_seq = (uint32_t)seq_ul;
    if (snap && snap->count > 0) {
      oldest = snap->oldest_seq;
      last = oldest + (uint32_t)snap->count - 1u;
      if (req_seq >= oldest && req_seq <= last) {
        const hls_seg_t *seg = &snap->segs[(snap->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
        r->body = seg->data;
        r->body_len = seg->size;
        r->content_type = !strcmp(ext, "m4s") ? "video/mp4" : "video/mp2t";
        seg_etag(seg->seq, seg->size, r->etag, sizeof r->etag);
      }
    }
  }
  if (!r->body) {
    r->status = 404;
    return 1;
  }
  r->status = if_none_match && !strcmp(if_none_match, r->etag) ? 304 : 200;
  return 1;
}

int hls_render(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!hls_resolve(ctx, filter, pmt_pid, lcevc, container, filename, if_none_match, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
  } else if (r.status == 304) {
    resp_set(out, 304, NULL, r.etag, NULL, 0, is_head);
  } else if (r.kind == HLS_RESOLVE_SEGMENT) {
    resp_set_zc(out, 200, r.content_type, r.etag, r.body, r.body_len, is_head);
  } else {
    resp_set(out, 200, r.content_type, r.kind == HLS_RESOLVE_INIT ? r.etag : NULL, r.body, r.body_len, is_head);
  }
  return 1;
}

#define HLS_LL_PLAYLIST_BUF_CAP 16384

int hls_resolve_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc,
                    const char *filename, const char *if_none_match, hls_ll_resolve_t *r) {
  hls_store_t *s;
  hls_snapshot_t *snap;
  uint32_t req_seq;
  int req_part;
  memset(r, 0, sizeof *r);
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_TS);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;

  if (!strcmp(filename, "index_ll.m3u8")) {
    r->kind = HLS_RESOLVE_PLAYLIST;
    if (!snap || snap->part_target <= 0.0 || (snap->count == 0 && snap->live_parts.count == 0)) {
      r->status = 404;
      return 1;
    }
    if (lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
      lcevc_master_snap_t msnap;
      const cached_text_t *ct;
      snapshot_lcevc_master(snap, &msnap);
      ct = snapshot_cache_text(&snap->cache_lcevc[1], fmt_lcevc_cb, &msnap, HLS_LL_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (const uint8_t *)ct->text;
      r->body_len = ct->len;
    } else {
      ll_playlist_snap_t llsnap;
      const cached_text_t *ct;
      snapshot_ll_playlist(snap, &llsnap);
      ct = snapshot_cache_text(&snap->cache_ll, fmt_ll_cb, &llsnap, HLS_LL_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (const uint8_t *)ct->text;
      r->body_len = ct->len;
    }
    r->status = 200;
    r->content_type = "application/vnd.apple.mpegurl";
    return 1;
  }
  if (!parse_part_filename(filename, &req_seq, &req_part)) return 0;
  r->kind = HLS_RESOLVE_SEGMENT;
  if (snap && snap->live_msn == req_seq && req_part < snap->live_parts.count) {
    r->body = snap->live_data + snap->live_parts.offset[req_part];
    r->body_len = snap->live_parts.size[req_part];
    part_etag(req_seq, req_part, r->body_len, r->etag, sizeof r->etag);
  } else if (snap && snap->count > 0) {
    uint32_t oldest = snap->oldest_seq;
    uint32_t last = oldest + (uint32_t)snap->count - 1u;
    if (req_seq >= oldest && req_seq <= last) {
      const hls_seg_t *seg = &snap->segs[(snap->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      if (req_part < seg->parts.count) {
        r->body = seg->data + seg->parts.offset[req_part];
        r->body_len = seg->parts.size[req_part];
        part_etag(req_seq, req_part, r->body_len, r->etag, sizeof r->etag);
      }
    }
  }
  if (!r->body) {
    r->status = 404;
    return 1;
  }
  r->content_type = "video/mp2t";
  r->status = if_none_match && !strcmp(if_none_match, r->etag) ? 304 : 200;
  return 1;
}

int hls_render_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_ll_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!hls_resolve_ll(ctx, filter, pmt_pid, lcevc, filename, if_none_match, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
  } else if (r.status == 304) {
    resp_set(out, 304, NULL, r.etag, NULL, 0, is_head);
  } else {
    resp_set(out, 200, r.content_type, r.kind == HLS_RESOLVE_PLAYLIST ? NULL : r.etag, r.body, r.body_len, is_head);
  }
  return 1;
}
