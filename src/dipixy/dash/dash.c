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
  for (; *s; s++) {
    switch (*s) {
      case '&': dst = write_lit(dst, "&amp;", 5); break;
      case '<': dst = write_lit(dst, "&lt;", 4); break;
      case '>': dst = write_lit(dst, "&gt;", 4); break;
      case '"': dst = write_lit(dst, "&quot;", 6); break;
      case '\'': dst = write_lit(dst, "&apos;", 6); break;
      default: *dst++ = *s; break;
    }
  }
  return dst;
}

/* avc1.PPCCLL from AVCProfileIndication/profile_compatibility/AVCLevelIndication in init seg's avcC box.
   hvcC needs 12 fixed-layout fields, hvc1.1.6.L93.B0 (Main, Level 3.1) generic fallback */
static void dash_codecs(const uint8_t *init, size_t initsz, int hevc, char *out, size_t outsz) {
  if (!hevc) {
    for (size_t i = 0; i + 8 <= initsz; i++) {
      if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == 'c' && init[i + 3] == 'C') {
        strbuf_t b;
        hls_sb_init(&b, out, outsz);
        hls_sb_add(&b, "avc1.");
        hls_sb_add_hex2(&b, init[i + 5]);
        hls_sb_add_hex2(&b, init[i + 6]);
        hls_sb_add_hex2(&b, init[i + 7]);
        return;
      }
    }
    bufcpy(out, outsz, "avc1.640028");
    return;
  }
  bufcpy(out, outsz, "hvc1.1.6.L93.B0");
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

/* caller holds store's lock. codecs: comma-joined video+audio (audio omitted if none).
   want_ll: route-selected, not derived from s->part_target */
static size_t build_mpd(const hls_store_t *s, char *mpd, size_t cap, int want_ll, const char *utc_url) {
  char *mp = mpd;
  char avail[32], publish[32], vcodec[32], acodec[32], codecs[64];
  double min_update, tsb_depth, pres_delay, min_buffer;
  uint64_t bw_bits = 0;
  double bw_secs = 0;
  int i;

  iso8601_utc(s->opened_at, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  dash_codecs(s->init_data, s->init_size, s->video_codec == 1, vcodec, sizeof vcodec);
  dash_audio_codecs(s->init_data, s->init_size, acodec, sizeof acodec);
  if (acodec[0]) {
    size_t off = bufcpy(codecs, sizeof codecs, vcodec);
    off += bufcpy(codecs + off, sizeof codecs - off, ",");
    bufcpy(codecs + off, sizeof codecs - off, acodec);
  } else {
    bufcpy(codecs, sizeof codecs, vcodec);
  }
  for (i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    bw_bits += (uint64_t)seg->size * 8;
    bw_secs += seg->duration;
  }

  /* 2 segs slack, not 1. real durations vary */
  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target)
    pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
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
  if (want_ll && s->part_target > 0.0)
    mp = WRITE_LIT(mp, "  <ServiceDescription id=\"0\">\n"
                        "    <Latency target=\"3500\" min=\"2000\" max=\"10000\" referenceId=\"0\"/>\n"
                        "  </ServiceDescription>\n");
  mp = WRITE_LIT(mp, "  <Period id=\"0\" start=\"PT0S\">\n"
                      "    <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n");
  if (want_ll && s->part_target > 0.0) {
    /* DASH-IF LL CR-r8 9.X.6.2.8 */
    mp = WRITE_LIT(mp, "      <Resync type=\"0\" dT=\"");
    mp = write_u64_gen(mp, (uint64_t)(s->part_target * 1000.0 + 0.5), 0);
    /* DASH-IF LL CR-r8 9.X.4.3/9.X.4.2 */
    mp = WRITE_LIT(mp, "\"/>\n      <ProducerReferenceTime id=\"0\" inband=\"true\" type=\"encoder\" wallclockTime=\"");
    mp = write_lit(mp, avail, strlen(avail));
    mp = WRITE_LIT(mp, "\" presentationTime=\"0\">\n        <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, utc_url);
    mp = WRITE_LIT(mp, "\"/>\n      </ProducerReferenceTime>\n");
  }
  mp = WRITE_LIT(mp, "      <Representation id=\"video\" codecs=\"");
  mp = write_lit(mp, codecs, strlen(codecs));
  mp = WRITE_LIT(mp, "\" bandwidth=\"");
  mp = write_u64_gen(mp, bw_secs > 0.0 ? (uint64_t)(bw_bits / bw_secs) : 1000000ULL, 0);
  mp = WRITE_LIT(mp, "\">\n        <SegmentTemplate initialization=\"init.mp4\" media=\"dseg$Time$.m4s\" timescale=\"1000\"");
  if (want_ll && s->part_target > 0.0) {
    double ato = s->seg_target - s->part_target;
    if (ato < 0.0) ato = 0.0;
    mp = WRITE_LIT(mp, " availabilityTimeOffset=\"");
    mp = write_fixed3(mp, ato);
    mp = WRITE_LIT(mp, "\" availabilityTimeComplete=\"false\"");
  }
  mp = WRITE_LIT(mp, ">\n          <SegmentTimeline>\n");

  /* seg dur varies (keyframe-aligned cuts): report true duration. t= only needed on the first entry, else implicit */
  for (i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    if ((size_t)(mp - mpd) + 64 > cap)
      break;
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

  mp = WRITE_LIT(mp, "          </SegmentTimeline>\n"
                      "        </SegmentTemplate>\n"
                      "      </Representation>\n"
                      "    </AdaptationSet>\n"
                      "  </Period>\n");
  if (want_ll && s->part_target > 0.0) {
    mp = WRITE_LIT(mp, "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, utc_url);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

int hls_serve_dash(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int want_ll, const char *utc_url, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  const hls_store_t *s;
  char mpd[8192];
  char cors_hdr[192];
  size_t mpd_len;

  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_FMP4);
  if (!s || s->count == 0) {
    if (s) pthread_mutex_unlock(store_lock(s));
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  mpd_len = build_mpd(s, mpd, sizeof mpd, want_ll, utc_url);
  pthread_mutex_unlock(store_lock(s));
  queue_mpd(c, mpd, mpd_len, is_head, keep_alive, cors_hdr);
  if (out_bytes)
    *out_bytes = mpd_len;
  return 1;
}

/* "dsegTTTT.m4s". 1 ok (t set), 0 wrong shape */
int parse_dash_seg_filename(const char *fn, uint64_t *t) {
  const char *p;
  char *end;
  if (strncmp(fn, "dseg", 4) != 0)
    return 0;
  p = fn + 4;
  if (*p < '0' || *p > '9')
    return 0;
  *t = strtoull(p, &end, 10);
  return !strcmp(end, ".m4s");
}

/* caller holds store's lock. NULL if no segment starts exactly at t_ms */
static const hls_seg_t *find_seg_by_time(const hls_store_t *s, uint64_t t_ms) {
  for (int i = 0; i < s->count; i++) {
    const hls_seg_t *seg = &s->segs[(s->head + i) % HLS_MAX_SEGS];
    if (seg->start_ms == t_ms) return seg;
  }
  return NULL;
}

int hls_serve_dash_seg(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int is_head, int keep_alive, const char *origin_hdr, size_t *out_bytes) {
  const hls_store_t *s;
  const hls_seg_t *seg;
  uint64_t req_t;
  char etag[48];
  char cors_hdr[192];
  if (!parse_dash_seg_filename(filename, &req_t)) return 0;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);

  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_FMP4);
  seg = s ? find_seg_by_time(s, req_t) : NULL;
  if (!seg) {
    if (s) pthread_mutex_unlock(store_lock(s));
    queue_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  seg_etag(seg->seq, seg->size, etag, sizeof etag);
  /* queue_segment() copies before unlock, queue_segment_zc() holds a ref already: both safe here */
  if (hls_zc_eligible(c, seg->size, is_head)) {
    seg_buf_ref(seg->data);
    queue_segment_zc(c, seg->data, seg->size, "video/mp4", etag, keep_alive, cors_hdr);
  } else {
    queue_segment(c, seg->data, seg->size, "video/mp4", etag, is_head, keep_alive, cors_hdr);
  }
  if (out_bytes)
    *out_bytes = seg->size;
  pthread_mutex_unlock(store_lock(s));
  return 1;
}

int hls_render_dash(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int want_ll, const char *utc_url, int is_head, hls_resp_t *out) {
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
  mpd_len = build_mpd(s, mpd, sizeof mpd, want_ll, utc_url);
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
