/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "mpd.h"

#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
#include "lib/hls/m3u_lines.h"

typedef struct {
  char init_url[2048];
  char media_url_tmpl[2048];
  unsigned timescale;
  unsigned long long duration;
  unsigned long long start_number;
  unsigned long long availability_time_offset_ms;
} segtmpl_defaults_t;

static unsigned long long seconds_attr_to_ms(const char *v) { return (unsigned long long)(strtod(v, NULL) * 1000.0 + 0.5); }

static unsigned pt_seconds_to_ms(const char *v) {
  const char *p = strchr(v, 'T');
  return p ? (unsigned)(strtod(p + 1, NULL) * 1000.0 + 0.5) : 0;
}

static void parse_mpd_attrs(const char *mpd_tag, const char *mpd_end, dash_mpd_t *out) {
  char v[32];
  if (xml_tag_attr(mpd_tag, mpd_end, "type", v, sizeof v) == 0) out->is_dynamic = !strcmp(v, "dynamic");
  if (xml_tag_attr(mpd_tag, mpd_end, "minimumUpdatePeriod", v, sizeof v) == 0) out->minimum_update_period_ms = pt_seconds_to_ms(v);
}

static void parse_segment_template(const char *blk, const char *blk_end, const http_url_t *base, segtmpl_defaults_t *out) {
  xml_span_t st;
  char v[2048];
  if (xml_find_elem(blk, blk_end, "SegmentTemplate", &st)) return;
  if (xml_tag_attr(st.tag, st.end, "initialization", v, sizeof v) == 0) playlist_resolve_uri(base, v, out->init_url, sizeof out->init_url);
  if (xml_tag_attr(st.tag, st.end, "media", v, sizeof v) == 0) playlist_resolve_uri(base, v, out->media_url_tmpl, sizeof out->media_url_tmpl);
  if (xml_tag_attr(st.tag, st.end, "timescale", v, sizeof v) == 0) out->timescale = (unsigned)strtoul(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "duration", v, sizeof v) == 0) out->duration = strtoull(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "startNumber", v, sizeof v) == 0) out->start_number = strtoull(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "availabilityTimeOffset", v, sizeof v) == 0) out->availability_time_offset_ms = seconds_attr_to_ms(v);
}

static void parse_service_description(const char *body, const char *end, dash_mpd_t *out) {
  xml_span_t sd, lat, pr;
  char v[32];
  if (xml_find_elem(body, end, "ServiceDescription", &sd)) return;
  out->is_low_latency = 1;
  if (xml_find_elem(sd.tag, sd.end, "Latency", &lat) == 0) {
    if (xml_tag_attr(lat.tag, lat.end, "target", v, sizeof v) == 0) out->latency_target_ms = (unsigned)strtoul(v, NULL, 10);
    if (xml_tag_attr(lat.tag, lat.end, "min", v, sizeof v) == 0) out->latency_min_ms = (unsigned)strtoul(v, NULL, 10);
    if (xml_tag_attr(lat.tag, lat.end, "max", v, sizeof v) == 0) out->latency_max_ms = (unsigned)strtoul(v, NULL, 10);
  }
  if (xml_find_elem(sd.tag, sd.end, "PlaybackRate", &pr) == 0) {
    if (xml_tag_attr(pr.tag, pr.end, "min", v, sizeof v) == 0) out->playback_rate_min = strtod(v, NULL);
    if (xml_tag_attr(pr.tag, pr.end, "max", v, sizeof v) == 0) out->playback_rate_max = strtod(v, NULL);
  }
}

typedef struct {
  dash_adaptation_set_t *as;
  const http_url_t *base;
  const segtmpl_defaults_t *inherited;
} repr_ctx_t;

static int representation_cb(const char *tag, const char *blk_end, void *vctx) {
  repr_ctx_t *ctx = vctx;
  dash_representation_t *r;
  segtmpl_defaults_t st;
  char v[32];
  int cut = 0;
  if (ctx->as->n_representations >= DASH_MAX_REPRESENTATIONS) return 0;
  r = &ctx->as->representations[ctx->as->n_representations];
  memset(r, 0, sizeof *r);
  st = *ctx->inherited;
  if (xml_attr_chk(tag, blk_end, "id", r->id, sizeof r->id, &cut) == 0 && cut)
    log_line("mpd: Representation id truncated to %zu bytes", strlen(r->id));
  if (xml_attr(tag, blk_end, "bandwidth", v, sizeof v) == 0) r->bandwidth = (unsigned)strtoul(v, NULL, 10);
  if (xml_attr(tag, blk_end, "width", v, sizeof v) == 0) r->width = (unsigned)strtoul(v, NULL, 10);
  if (xml_attr(tag, blk_end, "height", v, sizeof v) == 0) r->height = (unsigned)strtoul(v, NULL, 10);
  parse_segment_template(tag, blk_end, ctx->base, &st);
  bufcpy(r->init_url, sizeof r->init_url, st.init_url);
  bufcpy(r->media_url_tmpl, sizeof r->media_url_tmpl, st.media_url_tmpl);
  r->timescale = st.timescale;
  r->duration = st.duration;
  r->start_number = st.start_number;
  r->availability_time_offset_ms = st.availability_time_offset_ms;
  ctx->as->n_representations++;
  return 0;
}

typedef struct {
  dash_period_t *period;
  const http_url_t *base;
} as_ctx_t;

static int adaptation_set_cb(const char *tag, const char *blk_end, void *vctx) {
  as_ctx_t *ctx = vctx;
  dash_adaptation_set_t *as;
  segtmpl_defaults_t defaults;
  repr_ctx_t rctx;
  if (ctx->period->n_adaptation_sets >= DASH_MAX_ADAPTATION_SETS) return 0;
  as = &ctx->period->adaptation_sets[ctx->period->n_adaptation_sets];
  memset(as, 0, sizeof *as);
  xml_attr(tag, blk_end, "mimeType", as->mime_type, sizeof as->mime_type);
  memset(&defaults, 0, sizeof defaults);
  parse_segment_template(tag, blk_end, ctx->base, &defaults);
  rctx.as = as;
  rctx.base = ctx->base;
  rctx.inherited = &defaults;
  for_each_xml_elem(tag, blk_end, "Representation", representation_cb, &rctx);
  ctx->period->n_adaptation_sets++;
  return 0;
}

typedef struct {
  dash_mpd_t *mpd;
  const http_url_t *base;
} period_ctx_t;

static int period_cb(const char *tag, const char *blk_end, void *vctx) {
  period_ctx_t *ctx = vctx;
  dash_period_t *period;
  as_ctx_t actx;
  if (ctx->mpd->n_periods >= DASH_MAX_PERIODS) return 0;
  period = &ctx->mpd->periods[ctx->mpd->n_periods];
  memset(period, 0, sizeof *period);
  actx.period = period;
  actx.base = ctx->base;
  for_each_xml_elem(tag, blk_end, "AdaptationSet", adaptation_set_cb, &actx);
  ctx->mpd->n_periods++;
  return 0;
}

int dash_mpd_parse(const char *body, const http_url_t *base, dash_mpd_t *out) {
  const char *end = body + strlen(body);
  period_ctx_t ctx;
  const char *mpd_tag;
  memset(out, 0, sizeof *out);
  mpd_tag = xml_find_start(body, end, "MPD");
  if (!mpd_tag) return 0;
  parse_mpd_attrs(mpd_tag, end, out);
  parse_service_description(body, end, out);
  ctx.mpd = out;
  ctx.base = base;
  for_each_xml_elem(body, end, "Period", period_cb, &ctx);
  for (unsigned p = 0; !out->is_low_latency && p < out->n_periods; p++)
    for (unsigned a = 0; !out->is_low_latency && a < out->periods[p].n_adaptation_sets; a++)
      for (unsigned r = 0; !out->is_low_latency && r < out->periods[p].adaptation_sets[a].n_representations; r++)
        if (out->periods[p].adaptation_sets[a].representations[r].availability_time_offset_ms) out->is_low_latency = 1;
  return out->n_periods > 0;
}

int dash_pick_highest(const dash_adaptation_set_t *as) {
  int best = -1;
  unsigned best_bw = 0;
  for (unsigned i = 0; i < as->n_representations; i++) {
    if (best < 0 || as->representations[i].bandwidth > best_bw) {
      best = (int)i;
      best_bw = as->representations[i].bandwidth;
    }
  }
  return best;
}

int dash_media_url(const dash_representation_t *r, unsigned long long number, unsigned long long time, char *out, size_t n) {
  const char *src = r->media_url_tmpl;
  size_t pos = 0;
  char num[21];
  while (*src && pos + 1 < n) {
    if (!strncmp(src, "$Number$", 8)) {
      u64_to_dec(num, number);
      pos += bufcpy(out + pos, n - pos, num);
      src += 8;
    } else if (!strncmp(src, "$Time$", 6)) {
      u64_to_dec(num, time);
      pos += bufcpy(out + pos, n - pos, num);
      src += 6;
    } else {
      out[pos++] = *src++;
    }
  }
  out[pos < n ? pos : n - 1] = '\0';
  return pos > 0;
}

unsigned long long dash_effective_availability_ms(unsigned long long nominal_availability_ms, unsigned long long availability_time_offset_ms) {
  return nominal_availability_ms > availability_time_offset_ms ? nominal_availability_ms - availability_time_offset_ms : 0;
}
