/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/dash/dash_int.h"

void iso8601_utc(time_t t, char *out, size_t outsz) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  strftime(out, outsz, "%Y-%m-%dT%H:%M:%SZ", &tmv);
}

static void iso8601_utc_ms(uint64_t ms, char *out, size_t outsz) {
  time_t t = (time_t)(ms / 1000u);
  struct tm tmv;
  size_t n;
  gmtime_r(&t, &tmv);
  n = strftime(out, outsz, "%Y-%m-%dT%H:%M:%S", &tmv);
  if (n) snprintf(out + n, outsz - n, ".%03uZ", (unsigned)(ms % 1000u));
}

/* anchored wall time of media time 0, else store open */
void mpd_avail_time(const hls_store_t *s, const hls_snapshot_t *snap, char *out, size_t outsz) {
  if (snap->avail_ms) iso8601_utc_ms(snap->avail_ms, out, outsz);
  else iso8601_utc(s->opened_at, out, outsz);
}

size_t xml_escaped_len(const char *s) {
  size_t n = 0;
  for (; *s; s++) switch (*s) {
    case '&': n += 5; break;
    case '<':
    case '>': n += 4; break;
    case '"': n += 6; break;
    case '\'': n += 6; break;
    default: n++; break;
  }
  return n;
}

/* no bounds check, caller reserves xml_escaped_len() */
char *write_xml_escaped(char *dst, const char *s) {
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

char *write_mpd_prolog(char *mp, const char *avail, const char *publish, double min_update, double tsb_depth, double pres_delay, double min_buffer, int want_ll, double part_target) {
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
char *write_ll_resync(char *mp, const char *avail, const char *utc_url, double part_target, unsigned prt_id) {
  mp = WRITE_LIT(mp, "      <Resync type=\"0\" dT=\"");
  mp = write_u64_gen(mp, (uint64_t)(part_target * 1000.0 + 0.5), 0);
  mp = WRITE_LIT(mp, "\"/>\n      <ProducerReferenceTime id=\"");
  mp = write_u32(mp, prt_id, 0);
  mp = WRITE_LIT(mp, "\" inband=\"true\" type=\"encoder\" wallclockTime=\"");
  mp = write_lit(mp, avail, strlen(avail));
  mp = WRITE_LIT(mp, "\" presentationTime=\"0\">\n        <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
  mp = write_xml_escaped(mp, utc_url);
  mp = WRITE_LIT(mp, "\"/>\n      </ProducerReferenceTime>\n");
  return mp;
}

/* new period per stream format break: media time continues, init changes */
char *write_period_open(char *mp, const hls_snapshot_t *snap) {
  if (!snap->epoch) return WRITE_LIT(mp, "  <Period id=\"0\" start=\"PT0S\">\n");
  mp = WRITE_LIT(mp, "  <Period id=\"");
  mp = write_u32(mp, snap->epoch, 0);
  mp = WRITE_LIT(mp, "\" start=\"PT");
  mp = write_fixed3(mp, (double)snap->epoch_base_ms / 1000.0);
  return WRITE_LIT(mp, "S\">\n");
}

static char *write_pto(char *mp, const hls_snapshot_t *snap) {
  if (!snap->epoch) return mp;
  mp = WRITE_LIT(mp, " presentationTimeOffset=\"");
  mp = write_u64_gen(mp, snap->epoch_base_ms, 0);
  return WRITE_LIT(mp, "\"");
}

static char *write_segment_timeline(char *mp, const char *mpd_start, size_t cap, const hls_snapshot_t *snap) {
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
    if ((size_t)(mp - mpd_start) + DASH_SEG_ENTRY_MAX > cap) break;
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

static char *write_cstr(char *mp, const char *s) {
  return write_lit(mp, s, strlen(s));
}

/* query: "" or "?lcevc=<alt>", appended to both names */
char *write_segment_template(char *mp, const char *mpd_start, size_t cap, const hls_store_t *s, const hls_snapshot_t *snap, const char *init, const char *media, const char *query, int ll) {
  mp = WRITE_LIT(mp, "        <SegmentTemplate initialization=\"");
  mp = write_cstr(mp, init);
  mp = write_cstr(mp, query);
  mp = WRITE_LIT(mp, "\" media=\"");
  mp = write_cstr(mp, media);
  mp = write_cstr(mp, query);
  mp = WRITE_LIT(mp, "\" timescale=\"1000\"");
  mp = write_pto(mp, snap);
  if (ll) {
    double ato = s->seg_target - snap->part_target;
    if (ato < 0.0) ato = 0.0;
    mp = WRITE_LIT(mp, " availabilityTimeOffset=\"");
    mp = write_fixed3(mp, ato);
    mp = WRITE_LIT(mp, "\" availabilityTimeComplete=\"false\"");
  }
  mp = WRITE_LIT(mp, ">\n          <SegmentTimeline>\n");
  mp = write_segment_timeline(mp, mpd_start, cap, snap);
  return WRITE_LIT(mp, "          </SegmentTimeline>\n"
    "        </SegmentTemplate>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n");
}

uint64_t mpd_bandwidth(const hls_snapshot_t *snap) {
  uint64_t bw_bits = 0;
  double bw_secs = 0;
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
    bw_bits += (uint64_t)seg->size * 8;
    bw_secs += seg->duration;
  }
  return bw_secs > 0.0 ? (uint64_t)(bw_bits / bw_secs) : 1000000ULL;
}

static char *write_hex_upper(char *mp, unsigned v, int digits) {
  static const char hex[] = "0123456789ABCDEF";
  for (int sh = (digits - 1) * 4; sh >= 0; sh -= 4) *mp++ = hex[(v >> sh) & 0xF];
  return mp;
}

static char *write_audio_channel_configuration(char *mp, hls_chcfg_t scheme, unsigned value) {
  switch (scheme) {
    case HLS_CHCFG_COUNT:
      mp = WRITE_LIT(mp, "        <AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"");
      mp = write_u32(mp, value, 0);
      break;
    case HLS_CHCFG_DOLBY_2011:
      mp = WRITE_LIT(mp, "        <AudioChannelConfiguration schemeIdUri=\"tag:dolby.com,2014:dash:audio_channel_configuration:2011\" value=\"");
      mp = write_hex_upper(mp, value, 4);
      break;
    case HLS_CHCFG_DOLBY_2015:
      mp = WRITE_LIT(mp, "        <AudioChannelConfiguration schemeIdUri=\"tag:dolby.com,2015:dash:audio_channel_configuration:2015\" value=\"");
      mp = write_hex_upper(mp, value, 6);
      break;
    case HLS_CHCFG_DTS_2012:
      mp = WRITE_LIT(mp, "        <AudioChannelConfiguration schemeIdUri=\"tag:dts.com,2014:dash:audio_channel_configuration:2012\" value=\"");
      mp = write_u32(mp, value, 0);
      break;
    case HLS_CHCFG_CICP:
      mp = WRITE_LIT(mp, "        <AudioChannelConfiguration schemeIdUri=\"urn:mpeg:mpegB:cicp:ChannelConfiguration\" value=\"");
      mp = write_u32(mp, value, 0);
      break;
    default:
      return mp;
  }
  return WRITE_LIT(mp, "\"/>\n");
}

/* split mode. cap: as write_segment_timeline() */
char *write_audio_set(char *mp, const char *mpd_start, size_t cap, const mpd_fmt_ctx_t *ctx, const char *avail, int ll, unsigned as_id) {
  unsigned rate = ctx->asnap->audio_rate;
  mp = WRITE_LIT(mp, "    <AdaptationSet");
  if (as_id) {
    mp = WRITE_LIT(mp, " id=\"");
    mp = write_u32(mp, as_id, 0);
    mp = WRITE_LIT(mp, "\"");
  }
  mp = WRITE_LIT(mp, " contentType=\"audio\" mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n");
  if (ll) mp = write_ll_resync(mp, avail, ctx->utc_url, ctx->snap->part_target, 1);
  mp = WRITE_LIT(mp, "      <Representation id=\"audio\" codecs=\"");
  mp = write_cstr(mp, ctx->snap->acodec_str);
  if (rate) {
    mp = WRITE_LIT(mp, "\" audioSamplingRate=\"");
    mp = write_u32(mp, rate, 0);
  }
  mp = WRITE_LIT(mp, "\" bandwidth=\"");
  mp = write_u64_gen(mp, mpd_bandwidth(ctx->asnap), 0);
  mp = WRITE_LIT(mp, "\">\n");
  mp = write_audio_channel_configuration(mp, ctx->asnap->audio_chcfg, ctx->asnap->audio_chcfg_value);
  return write_segment_template(mp, mpd_start, cap, ctx->s, ctx->asnap, "dinita.mp4", "dsega$Time$.m4s", "", ll);
}
