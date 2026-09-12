/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dash.h"
#include "dash_int.h"

#include "lib/helper/ioutil.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void iso8601_utc(time_t t, char *out, size_t outsz) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, outsz, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

/* no bounds check, caller sizes buffer */
static char *write_xml_escaped(char *dst, const char *s) {
  for (; *s; s++) switch (*s) {
    case '&': dst = write_lit(dst, "&amp;", 5); break;
    case '<': dst = write_lit(dst, "&lt;", 4); break;
    case '>': dst = write_lit(dst, "&gt;", 4); break;
    case '"': dst = write_lit(dst, "&quot;", 6); break;
    case '\'': dst = write_lit(dst, "&apos;", 6); break;
    default: *dst++ = *s; break;
  }
  return dst;
}

/* avc1.PPCCLL from AVCProfileIndication/profile_compatibility/AVCLevelIndication in init seg's avcC box.
   hvcC needs 12 fixed-layout fields, hvc1.1.6.L93.B0 (Main, Level 3.1) generic fallback */
static void dash_codecs(const uint8_t *init, size_t initsz, codec_t vcodec, char *out, size_t outsz) {
  if (vcodec == CODEC_HEVC) {
    bufcpy(out, outsz, "hvc1.1.6.L93.B0");
    return;
  }
  if (vcodec == CODEC_VVC) {
    bufcpy(out, outsz, "vvc1.1.L1.CQ");
    return;
  }
  for (size_t i = 0; i + 8 <= initsz; i++) if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == 'c' && init[i + 3] == 'C') {
    strbuf_t b;
    hls_sb_init(&b, out, outsz);
    hls_sb_add(&b, "avc1.");
    hls_sb_add_hex2(&b, init[i + 5]);
    hls_sb_add_hex2(&b, init[i + 6]);
    hls_sb_add_hex2(&b, init[i + 7]);
    return;
  }
  bufcpy(out, outsz, "avc1.640028");
}

/* if init has no audio track="". mp4a.40.<N>: N from the AAC ASC's top 5 bits, fixed offset into build_esds() layout */
static void dash_audio_codecs(const uint8_t *init, size_t initsz, char *out, size_t outsz) {
  out[0] = '\0';
  for (size_t i = 0; i + 4 <= initsz; i++) {
    if (!memcmp(init + i, "ac-3", 4)) {
      bufcpy(out, outsz, "ac-3");
      return;
    }
    if (!memcmp(init + i, "ec-3", 4)) {
      bufcpy(out, outsz, "ec-3");
      return;
    }
    if (!memcmp(init + i, "Opus", 4)) {
      bufcpy(out, outsz, "opus");
      return;
    }
    if (!memcmp(init + i, "esds", 4) && i + 31 <= initsz) {
      if (init[i + 15] == 0x40) {
        strbuf_t b;
        hls_sb_init(&b, out, outsz);
        hls_sb_add(&b, "mp4a.40.");
        hls_sb_add_u64(&b, (uint64_t)(init[i + 30] >> 3));
      } else {
        bufcpy(out, outsz, "mp4a.6B");
      }
      return;
    }
  }
}

void dash_compute_codecs(const uint8_t *init, size_t initsz, codec_t vcodec, char *vcodec_out, size_t vcodec_outsz, char *acodec_out, size_t acodec_outsz) {
  dash_codecs(init, initsz, vcodec, vcodec_out, vcodec_outsz);
  dash_audio_codecs(init, initsz, acodec_out, acodec_outsz);
}

#define DASH_MPD_BUF_CAP 8192

typedef struct {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  int want_ll;
  const char *utc_url;
} mpd_fmt_ctx_t;

static char *write_mpd_prolog(char *mp, const char *avail, const char *publish, double min_update, double tsb_depth, double pres_delay, double min_buffer, int want_ll, double part_target) {
  mp = WRITE_LIT(mp, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
    "     profiles=\"urn:mpeg:dash:profile:isoff-live:2011\"\n"
    "     type=\"dynamic\"\n"
    "     availabilityStartTime=\"");
  mp = write_lit(mp, avail, strlen(avail));
  mp = WRITE_LIT(mp, "\"\n     publishTime=\"");
  mp = write_lit(mp, publish, strlen(publish));
  mp = WRITE_LIT(mp, "\"\n     minimumUpdatePeriod=\"PT");
  mp = write_fixed1(mp, min_update);
  mp = WRITE_LIT(mp, "S\"\n     timeShiftBufferDepth=\"PT");
  mp = write_fixed1(mp, tsb_depth);
  mp = WRITE_LIT(mp, "S\"\n     suggestedPresentationDelay=\"PT");
  mp = write_fixed1(mp, pres_delay);
  mp = WRITE_LIT(mp, "S\"\n     minBufferTime=\"PT");
  mp = write_fixed1(mp, min_buffer);
  mp = WRITE_LIT(mp, "S\">\n");
  /* DASH-IF LL CR-r8 9.X.4.2 */
  if (want_ll && part_target > 0.0)
    mp = WRITE_LIT(mp, "  <ServiceDescription id=\"0\">\n"
      "    <Latency target=\"3500\" min=\"2000\" max=\"10000\" referenceId=\"0\"/>\n"
      "  </ServiceDescription>\n");
  return mp;
}

/* DASH-IF LL CR-r8 9.X.6.2.8/9.X.4.3/9.X.4.2, inside a SegmentTemplate's own AdaptationSet */
static char *write_ll_resync(char *mp, const char *avail, const char *utc_url, double part_target) {
  mp = WRITE_LIT(mp, "      <Resync type=\"0\" dT=\"");
  mp = write_u64_gen(mp, (uint64_t)(part_target * 1000.0 + 0.5), 0);
  mp = WRITE_LIT(mp, "\"/>\n      <ProducerReferenceTime id=\"0\" inband=\"true\" type=\"encoder\" wallclockTime=\"");
  mp = write_lit(mp, avail, strlen(avail));
  mp = WRITE_LIT(mp, "\" presentationTime=\"0\">\n        <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
  mp = write_xml_escaped(mp, utc_url);
  mp = WRITE_LIT(mp, "\"/>\n      </ProducerReferenceTime>\n");
  return mp;
}

static char *write_segment_timeline(char *mp, const char *mpd_start, size_t cap, const hls_snapshot_t *snap) {
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->segs[(snap->head + i) % HLS_MAX_SEGS];
    if ((size_t)(mp - mpd_start) + 64 > cap) break;
    if (i == 0) {
      mp = WRITE_LIT(mp, "            <S t=\"");
      mp = write_u64_gen(mp, seg->start_ms, 0);
      mp = WRITE_LIT(mp, "\" d=\"");
    } else {
      mp = WRITE_LIT(mp, "            <S d=\"");
    }
    mp = write_u64_gen(mp, (uint64_t)(seg->duration * 1000.0 + 0.5), 0);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  return mp;
}

static void mpd_codecs(const hls_snapshot_t *snap, char *codecs, size_t codecs_sz) {
  const char *vcodec = snap->vcodec_str[0] ? snap->vcodec_str : "avc1.640028";
  if (snap->acodec_str[0]) {
    size_t off = bufcpy(codecs, codecs_sz, vcodec);
    off += bufcpy(codecs + off, codecs_sz - off, ",");
    bufcpy(codecs + off, codecs_sz - off, snap->acodec_str);
  } else {
    bufcpy(codecs, codecs_sz, vcodec);
  }
}

static uint64_t mpd_bandwidth(const hls_snapshot_t *snap) {
  uint64_t bw_bits = 0;
  double bw_secs = 0;
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->segs[(snap->head + i) % HLS_MAX_SEGS];
    bw_bits += (uint64_t)seg->size * 8;
    bw_secs += seg->duration;
  }
  return bw_secs > 0.0 ? (uint64_t)(bw_bits / bw_secs) : 1000000ULL;
}

/* codecs: comma-joined video+audio (audio omitted if none). want_ll: route-selected, not derived from snap->part_target */
static size_t build_mpd(const hls_store_t *s, const hls_snapshot_t *snap, char *mpd, size_t cap, int want_ll, const char *utc_url) {
  char *mp = mpd;
  char avail[32];
  char publish[32];
  char codecs[64];
  double min_update, tsb_depth, pres_delay, min_buffer;

  iso8601_utc(s->opened_at, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);

  /* 2 segs slack, not 1. real durations vary */
  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, want_ll, snap->part_target);
  mp = WRITE_LIT(mp, "  <Period id=\"0\" start=\"PT0S\">\n    <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n");
  if (want_ll && snap->part_target > 0.0) mp = write_ll_resync(mp, avail, utc_url, snap->part_target);
  mp = WRITE_LIT(mp, "      <Representation id=\"video\" codecs=\"");
  mp = write_lit(mp, codecs, strlen(codecs));
  mp = WRITE_LIT(mp, "\" bandwidth=\"");
  mp = write_u64_gen(mp, mpd_bandwidth(snap), 0);
  mp = WRITE_LIT(mp, "\">\n        <SegmentTemplate initialization=\"init.mp4\" media=\"dseg$Time$.m4s\" timescale=\"1000\"");
  if (want_ll && snap->part_target > 0.0) {
    double ato = s->seg_target - snap->part_target;
    if (ato < 0.0) ato = 0.0;
    mp = WRITE_LIT(mp, " availabilityTimeOffset=\"");
    mp = write_fixed3(mp, ato);
    mp = WRITE_LIT(mp, "\" availabilityTimeComplete=\"false\"");
  }
  mp = WRITE_LIT(mp, ">\n          <SegmentTimeline>\n");
  mp = write_segment_timeline(mp, mpd, cap, snap);
  mp = WRITE_LIT(mp, "          </SegmentTimeline>\n"
    "        </SegmentTemplate>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n");
  if (want_ll && snap->part_target > 0.0) {
    mp = WRITE_LIT(mp, "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, utc_url);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

/* one AdaptationSet per alternative, reuse base timeline */
static size_t build_mpd_lcevc_all(const hls_store_t *s, const hls_snapshot_t *snap, char *mpd, size_t cap, int want_ll, const char *utc_url) {
  char *mp = mpd;
  char avail[32];
  char publish[32];
  char codecs[64];
  double min_update;
  double tsb_depth;
  double pres_delay;
  double min_buffer;
  uint64_t bandwidth;
  int alt_count = snap->lcevc_pid_count + 1;

  iso8601_utc(s->opened_at, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);
  bandwidth = mpd_bandwidth(snap);

  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, want_ll, snap->part_target);
  mp = WRITE_LIT(mp, "  <Period id=\"0\" start=\"PT0S\">\n");
  for (int alt = 0; alt < alt_count; alt++) {
    unsigned pid = alt == 0 ? 0 : snap->lcevc_pid[alt - 1];
    if ((size_t)(mp - mpd) + 512 > cap) break;
    mp = WRITE_LIT(mp, "    <AdaptationSet id=\"");
    mp = write_u32(mp, (uint32_t)(alt + 1), 0);
    mp = WRITE_LIT(mp, "\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n"
      "      <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:adaptation-set-switching:2016\" value=\"");
    for (int k = 0, first = 1; k < alt_count; k++) {
      if (k == alt) continue;
      if (!first) mp = WRITE_LIT(mp, ",");
      mp = write_u32(mp, (uint32_t)(k + 1), 0);
      first = 0;
    }
    mp = WRITE_LIT(mp, "\"/>\n");
    if (want_ll && snap->part_target > 0.0) mp = write_ll_resync(mp, avail, utc_url, snap->part_target);
    mp = WRITE_LIT(mp, "      <Representation id=\"");
    if (pid) mp = write_u32(mp, pid, 0); else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\" codecs=\"");
    mp = write_lit(mp, codecs, strlen(codecs));
    mp = WRITE_LIT(mp, "\" bandwidth=\"");
    mp = write_u64_gen(mp, bandwidth, 0);
    if (pid) mp = WRITE_LIT(mp, "\" dependencyId=\"base");
    mp = WRITE_LIT(mp, "\">\n        <SegmentTemplate initialization=\"init.mp4?lcevc=");
    if (pid) mp = write_u32(mp, pid, 0); else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\" media=\"dseg$Time$.m4s?lcevc=");
    if (pid) mp = write_u32(mp, pid, 0); else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\" timescale=\"1000\"");
    if (want_ll && snap->part_target > 0.0) {
      double ato = s->seg_target - snap->part_target;
      if (ato < 0.0) ato = 0.0;
      mp = WRITE_LIT(mp, " availabilityTimeOffset=\"");
      mp = write_fixed3(mp, ato);
      mp = WRITE_LIT(mp, "\" availabilityTimeComplete=\"false\"");
    }
    mp = WRITE_LIT(mp, ">\n          <SegmentTimeline>\n");
    mp = write_segment_timeline(mp, mpd, cap, snap);
    mp = WRITE_LIT(mp, "          </SegmentTimeline>\n"
      "        </SegmentTemplate>\n"
      "      </Representation>\n"
      "    </AdaptationSet>\n");
  }
  mp = WRITE_LIT(mp, "  </Period>\n");
  if (want_ll && snap->part_target > 0.0) {
    mp = WRITE_LIT(mp, "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, utc_url);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

static size_t fmt_mpd_cb(void *ctx_, char *buf, size_t cap) {
  const mpd_fmt_ctx_t *ctx = ctx_;
  return build_mpd(ctx->s, ctx->snap, buf, cap, ctx->want_ll, ctx->utc_url);
}

static size_t fmt_mpd_all_cb(void *ctx_, char *buf, size_t cap) {
  const mpd_fmt_ctx_t *ctx = ctx_;
  return build_mpd_lcevc_all(ctx->s, ctx->snap, buf, cap, ctx->want_ll, ctx->utc_url);
}

#define DASH_MPD_ALL_BUF_CAP (DASH_MPD_BUF_CAP * (PSI_LCEVC_MAX_LINKS + 1))

typedef struct {
  int status;
  uint8_t *body;
  size_t body_len;
} dash_mpd_resolve_t;

typedef struct {
  int status;
  char etag[48];
  uint8_t *body;
  size_t body_len;
} dash_seg_resolve_t;

static int dash_resolve_mpd(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url,
                            dash_mpd_resolve_t *r) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  hls_snapshot_t *snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  mpd_fmt_ctx_t fctx;
  const cached_text_t *ct;
  memset(r, 0, sizeof *r);
  if (!snap || snap->count == 0) {
    r->status = 404;
    return 1;
  }
  fctx.s = s;
  fctx.snap = snap;
  fctx.want_ll = want_ll;
  fctx.utc_url = utc_url;
  if (lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0)
    ct = snapshot_cache_text(want_ll ? &snap->cache_mpd_ll : &snap->cache_mpd, fmt_mpd_all_cb, &fctx, DASH_MPD_ALL_BUF_CAP);
  else
    ct = snapshot_cache_text(want_ll ? &snap->cache_mpd_ll : &snap->cache_mpd, fmt_mpd_cb, &fctx, DASH_MPD_BUF_CAP);
  if (!ct) {
    r->status = 404;
    return 1;
  }
  r->status = 200;
  r->body = (uint8_t *)ct->text;
  r->body_len = ct->len;
  return 1;
}

int dash_serve(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  dash_mpd_resolve_t r;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  dash_resolve_mpd(ctx, filter, pmt_pid, lcevc, want_ll, utc_url, &r);
  if (r.status == 404) {
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  queue_mpd(c, (const char *)r.body, r.body_len, is_head, keep_alive, cors_hdr);
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}

/* "dsegTTTT.m4s". 1 ok (t set), 0 wrong shape */
int parse_dash_seg_filename(const char *fn, uint64_t *t) {
  const char *p;
  char *end;
  if (strncmp(fn, "dseg", 4) != 0) return 0;
  p = fn + 4;
  if (*p < '0' || *p > '9') return 0;
  *t = strtoull(p, &end, 10);
  return !strcmp(end, ".m4s");
}

/* NULL if no segment starts exactly at t_ms */
static const hls_seg_t *find_seg_by_time(const hls_snapshot_t *snap, uint64_t t_ms) {
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->segs[(snap->head + i) % HLS_MAX_SEGS];
    if (seg->start_ms == t_ms) return seg;
  }
  return NULL;
}

static int dash_resolve_seg(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, dash_seg_resolve_t *r) {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  const hls_seg_t *seg;
  uint64_t req_t;

  memset(r, 0, sizeof *r);
  if (!parse_dash_seg_filename(filename, &req_t)) return 0;
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  seg = snap ? find_seg_by_time(snap, req_t) : NULL;
  if (!seg) {
    r->status = 404;
    return 1;
  }
  r->status = 200;
  r->body = seg->data;
  r->body_len = seg->size;
  seg_etag(seg->seq, seg->size, r->etag, sizeof r->etag);
  return 1;
}

int dash_serve_seg(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  char cors_hdr[192];
  dash_seg_resolve_t r;
  if (!dash_resolve_seg(ctx, filter, pmt_pid, lcevc, filename, &r)) return 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  if (r.status == 404) {
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  if (hls_zc_eligible(c, r.body_len, is_head)) {
    seg_buf_ref(r.body);
    queue_segment_zc(c, r.body, r.body_len, "video/mp4", r.etag, keep_alive, cors_hdr);
  } else {
    queue_segment(c, r.body, r.body_len, "video/mp4", r.etag, is_head, keep_alive, cors_hdr);
  }
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}

int dash_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url, int is_head, hls_resp_t *out) {
  dash_mpd_resolve_t r;
  memset(out, 0, sizeof *out);
  dash_resolve_mpd(ctx, filter, pmt_pid, lcevc, want_ll, utc_url, &r);
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  resp_set(out, 200, "application/dash+xml", NULL, r.body, r.body_len, is_head);
  return 1;
}

int dash_render_seg(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int is_head, hls_resp_t *out) {
  dash_seg_resolve_t r;
  memset(out, 0, sizeof *out);
  if (!dash_resolve_seg(ctx, filter, pmt_pid, lcevc, filename, &r)) return 0;
  if (r.status == 404) {
    resp_set(out, 404, NULL, NULL, NULL, 0, is_head);
    return 1;
  }
  resp_set_zc(out, 200, "video/mp4", r.etag, r.body, r.body_len, is_head);
  return 1;
}
