/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "dash.h"
#include "dash_int.h"

#include "lib/sys/ioutil.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void iso8601_utc(time_t t, char *out, size_t outsz) {
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
static void mpd_avail_time(const hls_store_t *s, const hls_snapshot_t *snap, char *out, size_t outsz) {
  if (snap->avail_ms) iso8601_utc_ms(snap->avail_ms, out, outsz);
  else iso8601_utc((time_t)s->opened_at, out, outsz);
}

static size_t xml_escaped_len(const char *s) {
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
  if (vcodec == CODEC_AV1) {
    for (size_t i = 0; i + 7 <= initsz; i++) {
      if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == '1' && init[i + 3] == 'C') {
        unsigned pl = init[i + 5];
        unsigned th = init[i + 6];
        unsigned profile = (pl >> 5) & 0x07;
        unsigned level = pl & 0x1F;
        unsigned tier = (th >> 7) & 1;
        unsigned hbd = (th >> 6) & 1;
        unsigned twelve = (th >> 5) & 1;
        unsigned bitdepth;
        if (twelve) bitdepth = 12;
        else if (hbd) bitdepth = 10;
        else bitdepth = 8;
        snprintf(out, outsz, "av01.%u.%02u%c.%02u", profile, level, tier ? 'H' : 'M', bitdepth);
        return;
      }
    }
    bufcpy(out, outsz, "av01.0.04M.08");
    return;
  }
  for (size_t i = 0; i + 8 <= initsz; i++) {
    if (init[i] == 'a' && init[i + 1] == 'v' && init[i + 2] == 'c' && init[i + 3] == 'C') {
      sbuf_t b;
      sbuf_init(&b, out, outsz);
      sbuf_add(&b, "avc1.");
      sbuf_add_hex2(&b, init[i + 5]);
      sbuf_add_hex2(&b, init[i + 6]);
      sbuf_add_hex2(&b, init[i + 7]);
      return;
    }
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
    if (!memcmp(init + i, "dtsc", 4) || !memcmp(init + i, "dtsh", 4) || !memcmp(init + i, "dtse", 4) || !memcmp(init + i, "dtsl", 4)) {
      char fourcc[5];
      memcpy(fourcc, init + i, 4);
      fourcc[4] = '\0';
      bufcpy(out, outsz, fourcc);
      return;
    }
    if (!memcmp(init + i, "mlpa", 4)) {
      bufcpy(out, outsz, "mlpa");
      return;
    }
    if (!memcmp(init + i, "dac4", 4) && i + 6 <= initsz) {
      sbuf_t b;
      unsigned bs_ver = (unsigned)(((init[i + 4] & 0x1F) << 2) | (init[i + 5] >> 6));
      sbuf_init(&b, out, outsz);
      sbuf_add(&b, "ac-4.");
      sbuf_add_hex2(&b, (unsigned char)bs_ver);
      sbuf_add(&b, ".00.00");
      return;
    }
    if (!memcmp(init + i, "esds", 4) && i + 31 <= initsz) {
      if (init[i + 15] == 0x40) {
        sbuf_t b;
        sbuf_init(&b, out, outsz);
        sbuf_add(&b, "mp4a.40.");
        sbuf_add_u64(&b, (uint64_t)(init[i + 30] >> 3));
      } else bufcpy(out, outsz, "mp4a.6B");
      return;
    }
  }
}

void dash_compute_codecs(const uint8_t *init, size_t initsz, codec_t vcodec, char *vcodec_out, size_t vcodec_outsz, char *acodec_out, size_t acodec_outsz) {
  dash_codecs(init, initsz, vcodec, vcodec_out, vcodec_outsz);
  dash_audio_codecs(init, initsz, acodec_out, acodec_outsz);
}

#define DASH_MPD_BUF_CAP 8192
/* worst-case fixed text: head = prolog through Representation open, tail = closers after timeline,
   audio = split audio AdaptationSet without its timeline and utc url */
#define DASH_MPD_HEAD_MAX 2560
#define DASH_MPD_TAIL_MAX 512
#define DASH_MPD_AUDIO_MAX 1280
#define DASH_SEG_ENTRY_MAX 80

_Static_assert(DASH_MPD_BUF_CAP > DASH_MPD_HEAD_MAX + DASH_MPD_AUDIO_MAX + DASH_MPD_TAIL_MAX, "mpd cap");

/* vsnap, asnap: track stores. both set or both NULL: one muxed set from snap */
typedef struct {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  const hls_snapshot_t *vsnap;
  const hls_snapshot_t *asnap;
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
static char *write_ll_resync(char *mp, const char *avail, const char *utc_url, double part_target, unsigned prt_id) {
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
static char *write_period_open(char *mp, const hls_snapshot_t *snap) {
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
static char *write_segment_template(char *mp, const char *mpd_start, size_t cap, const hls_store_t *s, const hls_snapshot_t *snap, const char *init, const char *media, const char *query, int ll) {
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

static uint64_t mpd_bandwidth(const hls_snapshot_t *snap) {
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
static char *write_audio_set(char *mp, const char *mpd_start, size_t cap, const mpd_fmt_ctx_t *ctx, const char *avail, int ll, unsigned as_id) {
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

static void mpd_codecs(const hls_snapshot_t *snap, char *codecs, size_t codecs_sz) {
  bufcpy(codecs, codecs_sz, snap->vcodec_str[0] ? snap->vcodec_str : "avc1.640028");
}


/* want_ll: route-selected, not derived from snap->part_target. split: video and audio as separate AdaptationSets */
static size_t build_mpd(const mpd_fmt_ctx_t *ctx, char *mpd, size_t cap) {
  const hls_store_t *s = ctx->s;
  const hls_snapshot_t *snap = ctx->snap;
  const hls_snapshot_t *vsnap = ctx->vsnap ? ctx->vsnap : snap;
  int split = ctx->asnap != NULL;
  char *mp = mpd;
  char avail[32];
  char publish[32];
  char codecs[64];
  double min_update, tsb_depth, pres_delay, min_buffer;
  int ll = ctx->want_ll && snap->part_target > 0.0;
  size_t utc_len = ll ? xml_escaped_len(ctx->utc_url) : 0;
  size_t audio_max = split ? DASH_MPD_AUDIO_MAX + utc_len : 0;

  /* url written once per AdaptationSet and once in tail, omit LL timing if it cannot fit */
  if (ll && DASH_MPD_HEAD_MAX + audio_max + 2 * utc_len + DASH_MPD_TAIL_MAX > cap) {
    ll = 0;
    utc_len = 0;
    audio_max = split ? DASH_MPD_AUDIO_MAX : 0;
  }
  mpd_avail_time(s, vsnap, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);

  /* 2 segs slack, not 1. real durations vary */
  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, ll, snap->part_target);
  mp = write_period_open(mp, vsnap);
  if (split) mp = WRITE_LIT(mp, "    <AdaptationSet contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n");
  else mp = WRITE_LIT(mp, "    <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n");
  if (ll) mp = write_ll_resync(mp, avail, ctx->utc_url, snap->part_target, 0);
  mp = WRITE_LIT(mp, "      <Representation id=\"video\" codecs=\"");
  mp = write_lit(mp, codecs, strlen(codecs));
  mp = WRITE_LIT(mp, "\" bandwidth=\"");
  mp = write_u64_gen(mp, mpd_bandwidth(vsnap), 0);
  mp = WRITE_LIT(mp, "\">\n");
  mp = split ? write_segment_template(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len - audio_max, s, vsnap, "dinitv.mp4", "dsegv$Time$.m4s", "", ll)
             : write_segment_template(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len, s, vsnap, "init.mp4", "dseg$Time$.m4s", "", ll);
  if (split) mp = write_audio_set(mp, mpd, cap - DASH_MPD_TAIL_MAX - utc_len, ctx, avail, ll, 0);
  mp = WRITE_LIT(mp, "  </Period>\n");
  if (ll) {
    mp = WRITE_LIT(mp, "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, ctx->utc_url);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

/* one AdaptationSet per alternative, reuse base timeline */
static size_t build_mpd_lcevc_all(const mpd_fmt_ctx_t *ctx, char *mpd, size_t cap) {
  const hls_store_t *s = ctx->s;
  const hls_snapshot_t *snap = ctx->snap;
  const hls_snapshot_t *vsnap = ctx->vsnap ? ctx->vsnap : snap;
  int split = ctx->asnap != NULL;
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
  int ll = ctx->want_ll && snap->part_target > 0.0;
  size_t utc_len = ll ? xml_escaped_len(ctx->utc_url) : 0;
  size_t tail_max;
  size_t audio_max;

  audio_max = split ? DASH_MPD_AUDIO_MAX + utc_len : 0;
  if (ll && DASH_MPD_HEAD_MAX + audio_max + 2 * utc_len + DASH_MPD_TAIL_MAX > cap) {
    ll = 0;
    utc_len = 0;
    audio_max = split ? DASH_MPD_AUDIO_MAX : 0;
  }
  tail_max = DASH_MPD_TAIL_MAX + utc_len;

  mpd_avail_time(s, vsnap, avail, sizeof avail);
  iso8601_utc(time(NULL), publish, sizeof publish);
  mpd_codecs(snap, codecs, sizeof codecs);
  bandwidth = mpd_bandwidth(vsnap);

  min_update = s->seg_target;
  tsb_depth = s->seg_target * s->max_segs;
  pres_delay = s->seg_target * (double)(s->max_segs - 2);
  if (pres_delay < s->seg_target) pres_delay = s->seg_target;
  min_buffer = s->seg_target * 2.0;
  mp = write_mpd_prolog(mp, avail, publish, min_update, tsb_depth, pres_delay, min_buffer, ll, snap->part_target);
  mp = write_period_open(mp, vsnap);
  for (int alt = 0; alt < alt_count; alt++) {
    unsigned pid = alt == 0 ? 0 : snap->lcevc_pid[alt - 1];
    char query[24];
    sbuf_t qb;
    if ((size_t)(mp - mpd) + DASH_MPD_HEAD_MAX + utc_len + tail_max + audio_max > cap) break;
    sbuf_init(&qb, query, sizeof query);
    sbuf_add(&qb, "?lcevc=");
    if (pid) sbuf_add_u64(&qb, pid);
    else sbuf_add(&qb, "base");
    mp = WRITE_LIT(mp, "    <AdaptationSet id=\"");
    mp = write_u32(mp, (uint32_t)(alt + 1), 0);
    mp = WRITE_LIT(mp, "\"");
    if (split) mp = WRITE_LIT(mp, " contentType=\"video\"");
    mp = WRITE_LIT(mp, " mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n"
      "      <SupplementalProperty schemeIdUri=\"urn:mpeg:dash:adaptation-set-switching:2016\" value=\"");
    for (int k = 0, first = 1; k < alt_count; k++) {
      if (k == alt) continue;
      if (!first) mp = WRITE_LIT(mp, ",");
      mp = write_u32(mp, (uint32_t)(k + 1), 0);
      first = 0;
    }
    mp = WRITE_LIT(mp, "\"/>\n");
    if (ll) mp = write_ll_resync(mp, avail, ctx->utc_url, snap->part_target, 0);
    mp = WRITE_LIT(mp, "      <Representation id=\"");
    if (pid) mp = write_u32(mp, pid, 0); else mp = WRITE_LIT(mp, "base");
    mp = WRITE_LIT(mp, "\" codecs=\"");
    mp = write_lit(mp, codecs, strlen(codecs));
    mp = WRITE_LIT(mp, "\" bandwidth=\"");
    mp = write_u64_gen(mp, bandwidth, 0);
    if (pid) mp = WRITE_LIT(mp, "\" dependencyId=\"base");
    mp = WRITE_LIT(mp, "\">\n");
    mp = split ? write_segment_template(mp, mpd, cap - tail_max - audio_max, s, vsnap, "dinitv.mp4", "dsegv$Time$.m4s", query, ll)
               : write_segment_template(mp, mpd, cap - tail_max, s, vsnap, "init.mp4", "dseg$Time$.m4s", query, ll);
  }
  if (split) mp = write_audio_set(mp, mpd, cap - tail_max, ctx, avail, ll, (unsigned)alt_count + 1);
  mp = WRITE_LIT(mp, "  </Period>\n");
  if (ll) {
    mp = WRITE_LIT(mp, "  <UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsiso:2014\" value=\"");
    mp = write_xml_escaped(mp, ctx->utc_url);
    mp = WRITE_LIT(mp, "\"/>\n");
  }
  mp = WRITE_LIT(mp, "</MPD>\n");
  return (size_t)(mp - mpd);
}

static size_t fmt_mpd_cb(void *ctx_, char *buf, size_t cap) {
  return build_mpd(ctx_, buf, cap);
}

static size_t fmt_mpd_all_cb(void *ctx_, char *buf, size_t cap) {
  return build_mpd_lcevc_all(ctx_, buf, cap);
}

#define DASH_MPD_ALL_BUF_CAP (DASH_MPD_BUF_CAP * (PSI_LCEVC_MAX_LINKS + 1))

typedef struct {
  int status;
  uint8_t *body;
  size_t body_len;
  const cached_text_t *owned; /* private build, release after use */
} dash_mpd_resolve_t;

typedef struct {
  int status;
  char etag[48];
  const char *content_type;
  uint8_t *body;
  size_t body_len;
} dash_seg_resolve_t;

static const hls_snapshot_t *track_snap(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *t = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  return t ? atomic_load_explicit(&t->snap, memory_order_acquire) : NULL;
}

static int mpd_track_snaps(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, mpd_fmt_ctx_t *f) {
  const hls_snapshot_t *v;
  const hls_snapshot_t *a;
  f->vsnap = NULL;
  f->asnap = NULL;
  if (!f->snap->acodec_str[0]) return 1;
  v = track_snap(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4_VIDEO);
  a = track_snap(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4_AUDIO);
  if (!v || !a || v->count == 0 || a->count == 0 || !v->init_size || !a->init_size) return 0;
  if (v->epoch != f->snap->epoch || a->epoch != f->snap->epoch) return 0;
  f->vsnap = v;
  f->asnap = a;
  return 1;
}

static int dash_resolve_mpd(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int want_ll, const char *utc_url,
                            dash_mpd_resolve_t *r) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  hls_snapshot_t *snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  mpd_fmt_ctx_t fctx;
  const cached_text_t *ct;
  text_fmt_fn fmt;
  size_t cap;
  memset(r, 0, sizeof *r);
  if (!snap || snap->count == 0) {
    r->status = 404;
    return 1;
  }
  fctx.s = s;
  fctx.snap = snap;
  fctx.want_ll = want_ll;
  fctx.utc_url = utc_url;
  if (!mpd_track_snaps(ctx, filter, pmt_pid, lcevc, &fctx)) {
    r->status = 404;
    return 1;
  }
  if (lcevc->mode == LCEVC_SEL_ALL && snap->lcevc_pid_count > 0) {
    fmt = fmt_mpd_all_cb;
    cap = DASH_MPD_ALL_BUF_CAP;
  } else {
    fmt = fmt_mpd_cb;
    cap = DASH_MPD_BUF_CAP;
  }
  if (want_ll) {
    int owned;
    ct = snapshot_cache_text_keyed(snap->cache_mpd_ll, MPD_LL_VARIANTS, utc_url, fmt, &fctx, cap, &owned);
    if (owned) r->owned = ct;
  } else {
    ct = snapshot_cache_text(&snap->cache_mpd, fmt, &fctx, cap);
  }
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
    respond_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  queue_text_resp(c, "application/dash+xml", (const char *)r.body, r.body_len, is_head, keep_alive, cors_hdr);
  if (r.owned) cached_text_release(r.owned);
  if (out_bytes) *out_bytes = r.body_len;
  return 1;
}

/* "dsegTTTT.m4s" muxed, "dsegvTTTT.m4s" video track, "dsegaTTTT.m4s" audio track. 1 ok (t, container set), 0 wrong shape */
int parse_dash_seg_filename(const char *fn, uint64_t *t, seg_container_t *container) {
  const char *p;
  char *end;
  if (strncmp(fn, "dseg", 4) != 0) return 0;
  p = fn + 4;
  *container = SEG_CONTAINER_FMP4;
  if (*p == 'v') {
    *container = SEG_CONTAINER_FMP4_VIDEO;
    p++;
  } else if (*p == 'a') {
    *container = SEG_CONTAINER_FMP4_AUDIO;
    p++;
  }
  if (*p < '0' || *p > '9') return 0;
  *t = strtoull(p, &end, 10);
  return !strcmp(end, ".m4s");
}

/* "dinitv.mp4", "dinita.mp4": track store init segments */
static int parse_dash_init_filename(const char *fn, seg_container_t *container) {
  if (!strcmp(fn, "dinitv.mp4")) *container = SEG_CONTAINER_FMP4_VIDEO;
  else if (!strcmp(fn, "dinita.mp4")) *container = SEG_CONTAINER_FMP4_AUDIO;
  else return 0;
  return 1;
}

/* NULL if no segment starts exactly at t_ms */
static const hls_seg_t *find_seg_by_time(const hls_snapshot_t *snap, uint64_t t_ms) {
  for (int i = 0; i < snap->count; i++) {
    const hls_seg_t *seg = &snap->ring->segs[(snap->head + i) % HLS_MAX_SEGS];
    if (seg->start_ms == t_ms) return seg;
  }
  return NULL;
}

static int dash_resolve_seg(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, dash_seg_resolve_t *r) {
  const hls_store_t *s;
  const hls_snapshot_t *snap;
  const hls_seg_t *seg;
  seg_container_t container;
  uint64_t req_t;
  int init;

  memset(r, 0, sizeof *r);
  init = parse_dash_init_filename(filename, &container);
  if (!init && !parse_dash_seg_filename(filename, &req_t, &container)) return 0;
  r->content_type = container == SEG_CONTAINER_FMP4_AUDIO ? "audio/mp4" : "video/mp4";
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (init) {
    if (!snap || !snap->init_size) {
      r->status = 404;
      return 1;
    }
    r->status = 200;
    r->body = snap->init_data;
    r->body_len = snap->init_size;
    init_etag(snap->init_gen, snap->init_size, r->etag, sizeof r->etag);
    return 1;
  }
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
    respond_status(c, "404 Not Found", keep_alive);
    return 1;
  }
  if (hls_zc_eligible(c, r.body_len, is_head)) {
    seg_buf_ref(r.body);
    queue_segment_zc(c, r.body, r.body_len, r.content_type, r.etag, keep_alive, cors_hdr);
  } else {
    queue_segment(c, r.body, r.body_len, r.content_type, r.etag, is_head, keep_alive, cors_hdr);
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
  resp_set_zc_cached(out, 200, "application/dash+xml", NULL, r.body, r.body_len, is_head);
  if (r.owned) cached_text_release(r.owned);
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
  resp_set_zc(out, 200, r.content_type, r.etag, r.body, r.body, r.body_len, is_head);
  return 1;
}
