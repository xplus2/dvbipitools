/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "mpd.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

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
  unsigned long long presentation_time_offset;
  dash_timeline_entry_t timeline[DASH_MAX_TIMELINE];
  unsigned n_timeline;
  unsigned long long timeline_skipped;
} segtmpl_defaults_t;

static unsigned long long seconds_attr_to_ms(const char *v) { return (unsigned long long)(strtod(v, NULL) * 1000.0 + 0.5); }

/* xs:duration, Y and M(onth) approximated */
static unsigned long long duration_to_ms(const char *v) {
  double total = 0.0;
  int in_time = 0;
  if (*v != 'P') return 0;
  v++;
  while (*v) {
    char *e;
    double num;
    if (*v == 'T') {
      in_time = 1;
      v++;
      continue;
    }
    num = strtod(v, &e);
    if (e == v || !*e) break;
    switch (*e) {
      case 'Y': total += num * 365.0 * 86400.0; break;
      case 'W': total += num * 7.0 * 86400.0; break;
      case 'D': total += num * 86400.0; break;
      case 'H': total += num * 3600.0; break;
      case 'M': total += num * (in_time ? 60.0 : 30.0 * 86400.0); break;
      case 'S': total += num; break;
      default: return 0;
    }
    v = e + 1;
  }
  return total > 0.0 ? (unsigned long long)(total * 1000.0 + 0.5) : 0;
}

static int iso_datetime_to_ms(const char *v, unsigned long long *out) {
  iso8601_t t;
  long long secs;
  unsigned ms = 0;
  const char *dot;
  if (iso8601_split(v, &t)) return 0;
  secs = (date_to_mjd(t.y, t.mo, t.d) - 40587L) * 86400LL + t.h * 3600 + t.mi * 60 + t.s;
  if (t.offset_kind == ISO8601_OFF_NUMERIC) secs -= t.off_min * 60LL;
  if (secs < 0) return 0;
  dot = strchr(v, '.');
  if (dot) {
    unsigned scale = 100;
    for (dot++; *dot >= '0' && *dot <= '9' && scale; dot++, scale /= 10) ms += (unsigned)(*dot - '0') * scale;
  }
  *out = (unsigned long long)secs * 1000ULL + ms;
  return 1;
}

static void parse_mpd_attrs(const char *mpd_tag, const char *mpd_end, dash_mpd_t *out) {
  char v[64];
  if (xml_tag_attr(mpd_tag, mpd_end, "type", v, sizeof v) == 0) out->is_dynamic = !strcmp(v, "dynamic");
  if (xml_tag_attr(mpd_tag, mpd_end, "minimumUpdatePeriod", v, sizeof v) == 0) out->minimum_update_period_ms = (unsigned)duration_to_ms(v);
  if (xml_tag_attr(mpd_tag, mpd_end, "timeShiftBufferDepth", v, sizeof v) == 0) out->time_shift_buffer_ms = duration_to_ms(v);
  if (xml_tag_attr(mpd_tag, mpd_end, "availabilityStartTime", v, sizeof v) == 0) out->has_availability_start = iso_datetime_to_ms(v, &out->availability_start_ms);
}

typedef struct {
  segtmpl_defaults_t *st;
  int open_ended;
} timeline_ctx_t;

static int timeline_s_cb(const char *tag, const char *blk_end, void *vctx) {
  timeline_ctx_t *ctx = vctx;
  segtmpl_defaults_t *st = ctx->st;
  dash_timeline_entry_t e;
  char v[32];
  int has_t = xml_attr(tag, blk_end, "t", v, sizeof v) == 0;
  memset(&e, 0, sizeof e);
  if (has_t) e.t = strtoull(v, NULL, 10);
  if (xml_attr(tag, blk_end, "d", v, sizeof v) != 0) return 0;
  e.d = strtoull(v, NULL, 10);
  if (!e.d) return 0;
  if (st->n_timeline) {
    dash_timeline_entry_t *prev = &st->timeline[st->n_timeline - 1];
    if (!has_t) e.t = prev->t + prev->d * (prev->r + 1);
    else if (ctx->open_ended && e.t > prev->t) prev->r = (e.t - prev->t) / prev->d - 1;
  }
  ctx->open_ended = 0;
  if (xml_attr(tag, blk_end, "r", v, sizeof v) == 0) {
    if (v[0] == '-') ctx->open_ended = 1;
    else e.r = strtoull(v, NULL, 10);
  }
  if (st->n_timeline == DASH_MAX_TIMELINE) {
    st->timeline_skipped += 1 + st->timeline[0].r;
    memmove(st->timeline, st->timeline + 1, (DASH_MAX_TIMELINE - 1) * sizeof st->timeline[0]);
    st->n_timeline--;
  }
  st->timeline[st->n_timeline++] = e;
  return 0;
}

static void parse_segment_timeline(const xml_span_t *st_span, segtmpl_defaults_t *out) {
  xml_span_t tl;
  timeline_ctx_t ctx;
  if (xml_find_elem(st_span->tag, st_span->end, "SegmentTimeline", &tl)) return;
  out->n_timeline = 0;
  out->timeline_skipped = 0;
  ctx.st = out;
  ctx.open_ended = 0;
  for_each_xml_elem(tl.tag, tl.end, "S", timeline_s_cb, &ctx);
}

static const http_url_t *base_url_for(const http_url_t *parent, const char *s, const char *end, http_url_t *store) {
  char v[2048];
  char abs[2048];
  int cut = 0;
  xml_span_t b;
  if (xml_find_elem(s, end, "BaseURL", &b)) return parent;
  if (xml_span_text_chk(b.tag, b.end, v, sizeof v, &cut) || cut || !v[0]) return parent;
  playlist_resolve_uri(parent, v, abs, sizeof abs);
  if (!abs[0] || http_url_parse(abs, store)) return parent;
  return store;
}

static const char *children_start(const char *s, const char *end, const char *child) {
  const char *c = xml_find_start(s, end, child);
  return c ? c : end;
}

static void parse_segment_template(const char *blk, const char *blk_end, segtmpl_defaults_t *out) {
  xml_span_t st;
  char v[2048];
  if (xml_find_elem(blk, blk_end, "SegmentTemplate", &st)) return;
  parse_segment_timeline(&st, out);
  if (xml_tag_attr(st.tag, st.end, "presentationTimeOffset", v, sizeof v) == 0) out->presentation_time_offset = strtoull(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "initialization", v, sizeof v) == 0) bufcpy(out->init_url, sizeof out->init_url, v);
  if (xml_tag_attr(st.tag, st.end, "media", v, sizeof v) == 0) bufcpy(out->media_url_tmpl, sizeof out->media_url_tmpl, v);
  if (xml_tag_attr(st.tag, st.end, "timescale", v, sizeof v) == 0) out->timescale = (unsigned)strtoul(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "duration", v, sizeof v) == 0) out->duration = strtoull(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "startNumber", v, sizeof v) == 0) out->start_number = strtoull(v, NULL, 10);
  if (xml_tag_attr(st.tag, st.end, "availabilityTimeOffset", v, sizeof v) == 0) out->availability_time_offset_ms = seconds_attr_to_ms(v);
}

static void parse_service_description(const char *body, const char *end, dash_mpd_t *out) {
  xml_span_t sd;
  xml_span_t lat;
  xml_span_t pr;
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
  http_url_t rbase_store;
  const http_url_t *rbase;
  char v[32];
  int cut = 0;
  if (ctx->as->n_representations >= DASH_MAX_REPRESENTATIONS) return 0;
  r = &ctx->as->representations[ctx->as->n_representations];
  memset(r, 0, sizeof *r);
  st = *ctx->inherited;
  if (xml_attr_chk(tag, blk_end, "id", r->id, sizeof r->id, &cut) == 0 && cut)
    log_code("LW400142", "mpd: Representation id truncated to %zu bytes", strlen(r->id));
  if (xml_attr(tag, blk_end, "bandwidth", v, sizeof v) == 0) r->bandwidth = (unsigned)strtoul(v, NULL, 10);
  if (xml_attr(tag, blk_end, "width", v, sizeof v) == 0) r->width = (unsigned)strtoul(v, NULL, 10);
  if (xml_attr(tag, blk_end, "height", v, sizeof v) == 0) r->height = (unsigned)strtoul(v, NULL, 10);
  parse_segment_template(tag, blk_end, &st);
  rbase = base_url_for(ctx->base, tag, blk_end, &rbase_store);
  if (st.init_url[0]) playlist_resolve_uri(rbase, st.init_url, r->init_url, sizeof r->init_url);
  if (st.media_url_tmpl[0]) playlist_resolve_uri(rbase, st.media_url_tmpl, r->media_url_tmpl, sizeof r->media_url_tmpl);
  r->timescale = st.timescale;
  r->duration = st.duration;
  r->start_number = st.start_number;
  r->availability_time_offset_ms = st.availability_time_offset_ms;
  r->presentation_time_offset = st.presentation_time_offset;
  memcpy(r->timeline, st.timeline, st.n_timeline * sizeof st.timeline[0]);
  r->n_timeline = st.n_timeline;
  r->timeline_skipped = st.timeline_skipped;
  ctx->as->n_representations++;
  return 0;
}

typedef struct {
  dash_period_t *period;
  const http_url_t *base;
  const segtmpl_defaults_t *inherited;
} as_ctx_t;

static int adaptation_set_cb(const char *tag, const char *blk_end, void *vctx) {
  as_ctx_t *ctx = vctx;
  dash_adaptation_set_t *as;
  segtmpl_defaults_t defaults;
  http_url_t abase_store;
  const char *own_end = children_start(tag, blk_end, "Representation");
  repr_ctx_t rctx;
  if (ctx->period->n_adaptation_sets >= DASH_MAX_ADAPTATION_SETS) return 0;
  as = &ctx->period->adaptation_sets[ctx->period->n_adaptation_sets];
  memset(as, 0, sizeof *as);
  xml_attr(tag, blk_end, "mimeType", as->mime_type, sizeof as->mime_type);
  defaults = *ctx->inherited;
  parse_segment_template(tag, own_end, &defaults);
  rctx.as = as;
  rctx.base = base_url_for(ctx->base, tag, own_end, &abase_store);
  rctx.inherited = &defaults;
  for_each_xml_elem(tag, blk_end, "Representation", representation_cb, &rctx);
  ctx->period->n_adaptation_sets++;
  return 0;
}

typedef struct {
  dash_mpd_t *mpd;
  const http_url_t *base;
  unsigned long long prev_end_ms;
} period_ctx_t;

static int period_cb(const char *tag, const char *blk_end, void *vctx) {
  period_ctx_t *ctx = vctx;
  dash_period_t *period;
  as_ctx_t actx;
  segtmpl_defaults_t defaults;
  http_url_t pbase_store;
  const char *own_end = children_start(tag, blk_end, "AdaptationSet");
  char v[64];
  unsigned long long dur_ms = 0;
  if (ctx->mpd->n_periods >= DASH_MAX_PERIODS) return 0;
  period = &ctx->mpd->periods[ctx->mpd->n_periods];
  memset(period, 0, sizeof *period);
  period->start_ms = xml_tag_attr(tag, blk_end, "start", v, sizeof v) == 0 ? duration_to_ms(v) : ctx->prev_end_ms;
  if (xml_tag_attr(tag, blk_end, "duration", v, sizeof v) == 0) dur_ms = duration_to_ms(v);
  ctx->prev_end_ms = period->start_ms + dur_ms;
  memset(&defaults, 0, sizeof defaults);
  defaults.start_number = 1;
  parse_segment_template(tag, own_end, &defaults);
  actx.period = period;
  actx.base = base_url_for(ctx->base, tag, own_end, &pbase_store);
  actx.inherited = &defaults;
  for_each_xml_elem(tag, blk_end, "AdaptationSet", adaptation_set_cb, &actx);
  ctx->mpd->n_periods++;
  return 0;
}

int dash_mpd_parse(const char *body, const http_url_t *base, dash_mpd_t *out) {
  const char *end = body + strlen(body);
  period_ctx_t ctx;
  http_url_t mbase_store;
  const char *mpd_tag;
  memset(out, 0, offsetof(dash_mpd_t, periods)); /* periods[] reset per period_cb */
  mpd_tag = xml_find_start(body, end, "MPD");
  if (!mpd_tag) return 0;
  parse_mpd_attrs(mpd_tag, end, out);
  parse_service_description(body, end, out);
  ctx.mpd = out;
  ctx.base = base_url_for(base, mpd_tag, children_start(mpd_tag, end, "Period"), &mbase_store);
  ctx.prev_end_ms = 0;
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

static void put_str(char *out, size_t n, size_t *pos, const char *src, size_t len) {
  for (size_t i = 0; i < len && *pos + 1 < n; i++) out[(*pos)++] = src[i];
}

static void put_num(char *out, size_t n, size_t *pos, unsigned long long v, unsigned width) {
  char num[21];
  size_t len = u64_to_dec(num, v);
  for (; width > len; width--) put_str(out, n, pos, "0", 1);
  put_str(out, n, pos, num, len);
}

static int ident_is(const char *p, size_t len, const char *name) { return strlen(name) == len && !strncmp(p, name, len); }

static int expand_template(const char *src, const dash_representation_t *r, unsigned long long number, unsigned long long time, char *out, size_t n) {
  size_t pos = 0;
  while (*src) {
    const char *close;
    const char *fmt;
    size_t idlen;
    unsigned width = 0;
    close = *src == '$' ? strchr(src + 1, '$') : NULL;
    if (!close) {
      put_str(out, n, &pos, src++, 1);
      continue;
    }
    if (close == src + 1) {
      put_str(out, n, &pos, "$", 1);
      src += 2;
      continue;
    }
    fmt = memchr(src + 1, '%', (size_t)(close - src - 1));
    idlen = (size_t)((fmt ? fmt : close) - src - 1);
    if (fmt && fmt[1] == '0') width = (unsigned)strtoul(fmt + 2, NULL, 10);
    if (width > 20) width = 20;
    if (ident_is(src + 1, idlen, "RepresentationID")) put_str(out, n, &pos, r->id, strlen(r->id));
    else if (ident_is(src + 1, idlen, "Bandwidth")) put_num(out, n, &pos, r->bandwidth, width);
    else if (ident_is(src + 1, idlen, "Number")) put_num(out, n, &pos, number, width);
    else if (ident_is(src + 1, idlen, "Time")) put_num(out, n, &pos, time, width);
    else put_str(out, n, &pos, src, (size_t)(close - src + 1));
    src = close + 1;
  }
  out[pos < n ? pos : n - 1] = '\0';
  return pos > 0;
}

int dash_media_url(const dash_representation_t *r, unsigned long long number, unsigned long long time, char *out, size_t n) { return expand_template(r->media_url_tmpl, r, number, time, out, n); }

int dash_init_url(const dash_representation_t *r, char *out, size_t n) { return expand_template(r->init_url, r, 0, 0, out, n); }

static unsigned long long ticks_to_ms(const dash_representation_t *r, unsigned long long ticks) {
  unsigned long long ts = r->timescale ? r->timescale : 1;
  return ticks / ts * 1000ULL + ticks % ts * 1000ULL / ts;
}

unsigned long long dash_segment_duration_ms(const dash_representation_t *r) {
  if (r->n_timeline) return ticks_to_ms(r, r->timeline[r->n_timeline - 1].d);
  return ticks_to_ms(r, r->duration);
}

int dash_edge_index(const dash_representation_t *r, unsigned long long elapsed_ms, unsigned long long *idx) {
  unsigned long long ts = r->timescale ? r->timescale : 1;
  unsigned long long ms = elapsed_ms + r->availability_time_offset_ms;
  unsigned long long done;
  if (!r->duration) return 0;
  done = (ms / 1000ULL * ts + ms % 1000ULL * ts / 1000ULL) / r->duration;
  if (!done) return 0;
  *idx = done - 1;
  return 1;
}

unsigned long long dash_index_available_ms(const dash_representation_t *r, unsigned long long idx) {
  unsigned long long end = ticks_to_ms(r, (idx + 1) * r->duration);
  return end > r->availability_time_offset_ms ? end - r->availability_time_offset_ms : 0;
}

unsigned long long dash_index_time(const dash_representation_t *r, unsigned long long idx) { return r->presentation_time_offset + idx * r->duration; }

int dash_timeline_last(const dash_representation_t *r, unsigned long long *t, unsigned long long *d, unsigned long long *k) {
  const dash_timeline_entry_t *e;
  unsigned long long before = r->timeline_skipped;
  if (!r->n_timeline) return 0;
  for (unsigned i = 0; i + 1 < r->n_timeline; i++) before += 1 + r->timeline[i].r;
  e = &r->timeline[r->n_timeline - 1];
  *t = e->t + e->d * e->r;
  *d = e->d;
  *k = before + e->r;
  return 1;
}

int dash_timeline_find(const dash_representation_t *r, unsigned long long time, unsigned long long *d, unsigned long long *k) {
  unsigned long long before = r->timeline_skipped;
  for (unsigned i = 0; i < r->n_timeline; i++) {
    const dash_timeline_entry_t *e = &r->timeline[i];
    if (time >= e->t && time <= e->t + e->d * e->r && (time - e->t) % e->d == 0) {
      *d = e->d;
      *k = before + (time - e->t) / e->d;
      return 1;
    }
    before += 1 + e->r;
  }
  return 0;
}

unsigned long long dash_timeline_first(const dash_representation_t *r) { return r->n_timeline ? r->timeline[0].t : 0; }

unsigned long long dash_wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)ts.tv_nsec / 1000000ULL;
}

unsigned dash_mpd_live_period(const dash_mpd_t *m) {
  unsigned long long now;
  unsigned p = 0;
  if (!m->is_dynamic || !m->n_periods) return 0;
  if (!m->has_availability_start) return m->n_periods - 1;
  now = dash_wall_ms();
  now = now > m->availability_start_ms ? now - m->availability_start_ms : 0;
  for (unsigned i = 1; i < m->n_periods; i++)
    if (m->periods[i].start_ms <= now) p = i;
  return p;
}

unsigned long long dash_effective_availability_ms(unsigned long long nominal_availability_ms, unsigned long long availability_time_offset_ms) {
  return nominal_availability_ms > availability_time_offset_ms ? nominal_availability_ms - availability_time_offset_ms : 0;
}
