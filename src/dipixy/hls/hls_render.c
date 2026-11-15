/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "hls_int.h"

#include <stdint.h>
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

int hls_inm_match(const char *inm, const char *etag) {
  size_t elen = strlen(etag);
  const char *p = inm;
  if (!inm) return 0;
  for (;;) {
    const char *s;
    while (*p == ' ' || *p == '\t' || *p == ',') p++;
    if (!*p) return 0;
    if (!strncmp(p, "W/", 2)) p += 2;
    if (*p == '"') {
      s = ++p;
      while (*p && *p != '"') p++;
      if (!*p) return 0;
      if ((size_t)(p - s) == elen && !memcmp(s, etag, elen)) return 1;
      p++;
    } else {
      s = p;
      while (*p && *p != ',' && *p != ' ' && *p != '\t') p++;
      if (p - s == 1 && *s == '*') return 1;
      if ((size_t)(p - s) == elen && !memcmp(s, etag, elen)) return 1;
    }
  }
}

int hls_resolve(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, const char *if_none_match, hls_resolve_t *r) {
  hls_snapshot_t *snap;
  memset(r, 0, sizeof *r);
  if (f->kind != ROUTE_FILE_INDEX && f->kind != ROUTE_FILE_INIT && f->kind != ROUTE_FILE_SEG) return 0;
  snap = st ? atomic_load_explicit(&st->snap, memory_order_acquire) : NULL;
  if (f->kind == ROUTE_FILE_INDEX) {
    r->kind = HLS_RESOLVE_PLAYLIST;
    if (!snap || snap->count == 0) {
      r->status = 404;
      return 1;
    }
    if (k->lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
      lcevc_master_snap_t msnap;
      const cached_text_t *ct;
      snapshot_lcevc_master(snap, &msnap);
      ct = snapshot_cache_text(&snap->cache[SNAP_CACHE_LCEVC], fmt_lcevc_cb, &msnap, HLS_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (uint8_t *)ct->text;
      r->body_len = ct->len;
    } else {
      plain_playlist_snap_t psnap;
      const cached_text_t *ct;
      snapshot_plain_playlist(snap, k->container, k->lcevc, &psnap);
      ct = snapshot_cache_text(&snap->cache[SNAP_CACHE_PLAIN], fmt_plain_cb, &psnap, HLS_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (uint8_t *)ct->text;
      r->body_len = ct->len;
    }
    r->status = 200;
    r->content_type = "application/vnd.apple.mpegurl";
    return 1;
  }

  if (f->kind == ROUTE_FILE_INIT) {
    r->kind = HLS_RESOLVE_INIT;
    if (snap && snap->init_size) {
      r->body = snap->init_data;
      r->body_len = snap->init_size;
      r->content_type = "video/mp4";
      init_etag(snap->init_gen, snap->init_size, r->etag, sizeof r->etag);
    }
  } else {
    r->kind = HLS_RESOLVE_SEGMENT;
    if ((k->container == SEG_CONTAINER_FMP4) == f->fmp4 && snap && snap->count > 0) {
      uint32_t oldest = snap->oldest_seq;
      if (f->seq >= oldest && f->seq - oldest < (uint32_t)snap->count) {
        const hls_seg_t *seg = &snap->ring->segs[(snap->head + (int)(f->seq - oldest)) % HLS_MAX_SEGS];
        r->body = seg->data;
        r->body_len = seg->size;
        r->content_type = f->fmp4 ? "video/mp4" : "video/mp2t";
        seg_etag(seg->seq, seg->size, r->etag, sizeof r->etag);
      }
    }
  }
  if (!r->body) {
    r->status = 404;
    return 1;
  }
  r->status = hls_inm_match(if_none_match, r->etag) ? 304 : 200;
  return 1;
}

int hls_render(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!hls_resolve(k, st, f, if_none_match, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
  } else if (r.status == 304) {
    resp_set(out, 304, NULL, r.etag, NULL, 0, is_head);
  } else if (r.kind == HLS_RESOLVE_SEGMENT) {
    resp_set_zc(out, 200, r.content_type, r.etag, r.body, r.body, r.body_len, is_head);
  } else if (r.kind == HLS_RESOLVE_PLAYLIST) {
    resp_set_zc_cached(out, 200, r.content_type, NULL, r.body, r.body_len, is_head);
  } else {
    resp_set(out, 200, r.content_type, r.etag, r.body, r.body_len, is_head);
  }
  return 1;
}

#define HLS_LL_PLAYLIST_BUF_CAP 16384

int hls_resolve_ll(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, const char *if_none_match, hls_ll_resolve_t *r) {
  hls_snapshot_t *snap;
  uint32_t req_seq = f->seq;
  int req_part = f->part;
  memset(r, 0, sizeof *r);
  snap = st ? atomic_load_explicit(&st->snap, memory_order_acquire) : NULL;

  if (f->kind == ROUTE_FILE_INDEX_LL) {
    r->kind = HLS_RESOLVE_PLAYLIST;
    if (!snap || snap->part_target <= 0.0 || (snap->count == 0 && snap->live_parts.count == 0)) {
      r->status = 404;
      return 1;
    }
    if (k->lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
      lcevc_master_snap_t msnap;
      const cached_text_t *ct;
      snapshot_lcevc_master(snap, &msnap);
      ct = snapshot_cache_text(&snap->cache[SNAP_CACHE_LCEVC_LL], fmt_lcevc_cb, &msnap, HLS_LL_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (const uint8_t *)ct->text;
      r->body_len = ct->len;
    } else {
      ll_playlist_snap_t llsnap;
      const cached_text_t *ct;
      snapshot_ll_playlist(snap, k->container, k->lcevc, &llsnap);
      ct = snapshot_cache_text(&snap->cache[SNAP_CACHE_LL], fmt_ll_cb, &llsnap, HLS_LL_PLAYLIST_BUF_CAP);
      if (!ct) return 0;
      r->body = (const uint8_t *)ct->text;
      r->body_len = ct->len;
    }
    r->status = 200;
    r->content_type = "application/vnd.apple.mpegurl";
    return 1;
  }
  if (f->kind != ROUTE_FILE_PART || f->fmp4 != (k->container == SEG_CONTAINER_FMP4)) return 0;
  r->kind = HLS_RESOLVE_SEGMENT;
  if (snap && snap->live_msn == req_seq && req_part < snap->live_parts.count) {
    r->owner = snap->live_data;
    r->body = snap->live_data + snap->live_parts.offset[req_part];
    r->body_len = snap->live_parts.size[req_part];
    part_etag(req_seq, req_part, r->body_len, r->etag, sizeof r->etag);
  } else if (snap && snap->count > 0) {
    uint32_t oldest = snap->oldest_seq;
    if (req_seq >= oldest && req_seq - oldest < (uint32_t)snap->count) {
      const hls_seg_t *seg = &snap->ring->segs[(snap->head + (int)(req_seq - oldest)) % HLS_MAX_SEGS];
      if (req_part < seg->parts.count) {
        r->owner = seg->data;
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
  r->content_type = k->container == SEG_CONTAINER_FMP4 ? "video/mp4" : "video/mp2t";
  r->status = hls_inm_match(if_none_match, r->etag) ? 304 : 200;
  return 1;
}

int hls_render_ll(const stream_key_t *k, const hls_store_t *st, const route_file_t *f, int is_head, const char *if_none_match, hls_resp_t *out) {
  hls_ll_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!hls_resolve_ll(k, st, f, if_none_match, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
  } else if (r.status == 304) {
    resp_set(out, 304, NULL, r.etag, NULL, 0, is_head);
  } else if (r.kind == HLS_RESOLVE_PLAYLIST) {
    resp_set_zc_cached(out, 200, r.content_type, NULL, r.body, r.body_len, is_head);
  } else {
    resp_set_zc(out, 200, r.content_type, r.etag, r.owner, (uint8_t *)r.body, r.body_len, is_head);
  }
  return 1;
}
