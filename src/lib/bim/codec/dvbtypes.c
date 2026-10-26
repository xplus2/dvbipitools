/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/dateutil.h"
#include "lib/sys/ioutil.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

int dvb_string_encode(strrepo_writer_t *sw, const char *s) {
  return strrepo_writer_put(sw, s);
}

int dvb_string_decode(strrepo_reader_t *sr, char *out, size_t outcap) {
  return sr ? strrepo_reader_next(sr, out, outcap) : -1;
}

int dvb_string_append(strrepo_reader_t *sr, dstrbuf_t *b) {
  char *s;
  if (!sr || strrepo_reader_next_dup(sr, &s)) return -1;
  dstrbuf_add(b, s);
  free(s);
  return b->buf ? 0 : -1;
}

/* rc 0: b into out if it fits. b freed */
static int copy_out(dstrbuf_t *b, int rc, char *out, size_t outcap) {
  if (!rc && (!b->buf || b->len >= outcap)) rc = -1;
  if (!rc) memcpy(out, b->buf, b->len + 1);
  free(b->buf);
  return rc;
}

int dvb_string_decode_dup(strrepo_reader_t *sr, char **out) {
  char *s;
  if (!sr || strrepo_reader_next_dup(sr, &s)) return -1;
  if (!s[0]) {
    free(s);
    s = NULL;
  }
  free(*out);
  *out = s;
  return 0;
}

int dvb_flagged_string_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *s) {
  if (bitwriter_put(bw, 0, 1)) return -1;
  return dvb_string_encode(sw, s);
}

int dvb_locator_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *uri) {
  return dvb_flagged_string_encode(bw, sw, uri); /* optimized_codec_flag = 0, always string fallback */
}


typedef struct {
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  unsigned tags[32];
  int ntags;
  int has_cid;
  unsigned cid;
  int kind; /* 0 none, 1 event, 2 TVA id */
  unsigned id;
  int has_time;
  long mjd;
  unsigned tm;
  unsigned dur;
  const char *path;
} loc_t;

static int hex_run(const char **p, unsigned *v) {
  char *end;
  unsigned long x;
  if (!isxdigit((unsigned char)**p)) return -1;
  x = strtoul(*p, &end, 16);
  if (x > 0xFFFFFFFFUL) return -1;
  *p = end;
  *v = (unsigned)x;
  return 0;
}

/* grammar of locator_decode_optimized */
static int loc_parse(const char *s, loc_t *l) {
  const char *p = s;
  memset(l, 0, sizeof *l);
  if (strncmp(p, "dvb://", 6)) return -1;
  p += 6;
  if (hex_run(&p, &l->onid)) return -1;
  if (*p++ != '.') return -1;
  if (*p != '.' && hex_run(&p, &l->tsid)) return -1;
  if (*p++ != '.') return -1;
  if (hex_run(&p, &l->sid)) return -1;
  if (*p == '.') {
    for (char sep = '.'; *p == sep && l->ntags < 32; sep = '&') {
      p++;
      if (hex_run(&p, &l->tags[l->ntags++])) return -1;
    }
    if (*p == '$') {
      p++;
      l->has_cid = 1;
      if (hex_run(&p, &l->cid)) return -1;
    }
  }
  if (*p == ';') {
    p++;
    l->kind = 1;
    if (*p == ';') {
      p++;
      l->kind = 2;
    }
    if (hex_run(&p, &l->id)) return -1;
  }
  if (*p == '~') {
    int y;
    int mo;
    int d;
    unsigned hh;
    unsigned mm;
    unsigned ss;
    unsigned dh;
    unsigned dm;
    unsigned ds;
    int used;
    if (sscanf(p, "~%4d%2d%2dT%2u%2u%2uZ--PT%uH%uM%uS%n", &y, &mo, &d, &hh, &mm, &ss, &dh, &dm, &ds, &used) != 9) return -1;
    p += used;
    l->has_time = 1;
    l->mjd = date_to_mjd(y, mo, d);
    l->tm = hh * 3600 + mm * 60 + ss;
    l->dur = dh * 3600 + dm * 60 + ds;
  }
  if (*p == '/') {
    l->path = p + 1;
    p += strlen(p);
  }
  return *p ? -1 : 0;
}

static int loc_put(bitwriter_t *bw, strrepo_writer_t *sw, dvb_codec_state_t *st, const loc_t *l) {
  int prefix = !st->have_prefix || st->onid != l->onid || st->tsid != l->tsid || st->sid != l->sid;
  int day = l->has_time && (!st->have_day || st->day != (unsigned)(l->mjd & 0xFFFF));
  if (bitwriter_put(bw, 1, 1) || bitwriter_put(bw, (uint64_t)prefix, 1)) return -1;
  if (prefix && (bitwriter_put(bw, l->tsid, 16) || bitwriter_put(bw, l->onid, 16) || bitwriter_put(bw, l->sid, 16))) return -1;
  st->have_prefix = 1;
  st->onid = l->onid;
  st->tsid = l->tsid;
  st->sid = l->sid;
  if (bitwriter_put(bw, (uint64_t)(l->ntags > 0), 1)) return -1;
  for (int i = 0; i < l->ntags; i++)
    if (bitwriter_put(bw, l->tags[i], 8) || bitwriter_put(bw, (uint64_t)(i + 1 < l->ntags), 1)) return -1;
  if (bitwriter_put(bw, (uint64_t)l->has_cid, 1) || (l->has_cid && bitwriter_put(bw, l->cid, 32))) return -1;
  if (bitwriter_put(bw, (uint64_t)l->kind, 2) || (l->kind && bitwriter_put(bw, l->id, 16))) return -1;
  if (bitwriter_put(bw, (uint64_t)l->has_time, 1)) return -1;
  if (l->has_time) {
    if (bitwriter_put(bw, (uint64_t)day, 1) || (day && bitwriter_put(bw, (uint64_t)l->mjd & 0xFFFF, 16))) return -1;
    st->have_day = 1;
    st->day = (unsigned)(l->mjd & 0xFFFF);
    if (bitwriter_put(bw, l->tm, 17) || bitwriter_put(bw, l->dur, 17)) return -1;
  }
  if (bitwriter_put(bw, (uint64_t)(l->path != NULL), 1)) return -1;
  return l->path ? dvb_string_encode(sw, l->path) : 0;
}

/* 0 written, 1 not representable, -1 error */
static int loc_try(bitwriter_t *bw, strrepo_writer_t *sw, dvb_codec_state_t *st, const char *uri) {
  loc_t l;
  bitwriter_t tb;
  strrepo_writer_t ts;
  dvb_codec_state_t t1 = *st;
  dvb_codec_state_t t2 = *st;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *s;
  size_t bl;
  size_t sl;
  dstrbuf_t out = {NULL, 0, 0};
  int rc = 1;
  if (loc_parse(uri, &l) || (l.has_time && (l.mjd < MJD_UNIX_EPOCH || l.mjd >= MJD_UNIX_EPOCH + 65536L || l.tm >= 86400 || l.dur >= (1u << 17)))) return 1;
  bitwriter_init(&tb);
  if (strrepo_writer_init(&ts)) return -1;
  dstrbuf_init(&out);
  if (!out.buf) rc = -1;
  if (!out.buf || loc_put(&tb, &ts, &t1, &l)) goto done;
  b = bitwriter_data(&tb, &bl);
  s = strrepo_writer_data(&ts, &sl);
  bitreader_init(&br, b, bl);
  if (strrepo_reader_init(&sr, s, sl) || dvb_locator_decode_buf(&br, &sr, &t2, &out) || strcmp(out.buf, uri)) goto done;
  rc = loc_put(bw, sw, st, &l) ? -1 : 0;
done:
  free(out.buf);
  bitwriter_free(&tb);
  strrepo_writer_free(&ts);
  return rc;
}

int dvb_locator_encode_st(bitwriter_t *bw, strrepo_writer_t *sw, dvb_codec_state_t *st, const char *uri, int optimized) {
  int rc = optimized ? loc_try(bw, sw, st, uri) : 1;
  return rc > 0 ? dvb_locator_encode(bw, sw, uri) : rc;
}

static long mjd16_full(uint64_t v);

static void add_hex(dstrbuf_t *b, const char *pre, unsigned v) {
  char tmp[16];
  snprintf(tmp, sizeof tmp, "%s%x", pre, v);
  dstrbuf_add(b, tmp);
}

/* OptimizedDVBLocator() -> TS 102 851 6.4 text. prefix + day reuse via st */
static int locator_decode_optimized(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, dstrbuf_t *b) {
  uint64_t v;
  uint64_t cont = 0;
  uint64_t event_or_tva;
  int nctag = 0;
  char tmp[64];

  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    uint64_t ts;
    uint64_t on;
    uint64_t sid;
    if (bitreader_get(br, 16, &ts) || bitreader_get(br, 16, &on) || bitreader_get(br, 16, &sid)) return -1;
    st->have_prefix = 1;
    st->tsid = (unsigned)ts;
    st->onid = (unsigned)on;
    st->sid = (unsigned)sid;
  } else if (!st->have_prefix) {
    return -1;
  }
  add_hex(b, "dvb://", st->onid);
  dstrbuf_add(b, ".");
  if (st->tsid) add_hex(b, "", st->tsid);
  add_hex(b, ".", st->sid);
  if (bitreader_get(br, 1, &cont)) return -1;
  while (cont) {
    uint64_t tag;
    if (bitreader_get(br, 8, &tag) || bitreader_get(br, 1, &cont)) return -1;
    add_hex(b, nctag++ ? "&" : ".", (unsigned)tag);
  }
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    if (!nctag || bitreader_get(br, 32, &v)) return -1; /* "$" only after component_set */
    add_hex(b, "$", (unsigned)v);
  }
  if (bitreader_get(br, 2, &event_or_tva) || event_or_tva == 3) return -1;
  if (event_or_tva) {
    if (bitreader_get(br, 16, &v)) return -1;
    add_hex(b, event_or_tva == 1 ? ";" : ";;", (unsigned)v);
  }
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    uint64_t tm;
    uint64_t dur;
    int y;
    int mo;
    int d;
    if (bitreader_get(br, 1, &v)) return -1;
    if (v) {
      if (bitreader_get(br, 16, &v)) return -1;
      st->have_day = 1;
      st->day = (unsigned)v;
    } else if (!st->have_day) {
      return -1;
    }
    if (bitreader_get(br, 17, &tm) || bitreader_get(br, 17, &dur) || tm >= 86400) return -1;
    mjd_to_date(mjd16_full(st->day), &y, &mo, &d);
    snprintf(tmp, sizeof tmp, "~%04d%02d%02dT%02u%02u%02uZ--PT%02uH%02uM%02uS", y, mo, d, (unsigned)(tm / 3600), (unsigned)(tm / 60 % 60), (unsigned)(tm % 60),
             (unsigned)(dur / 3600), (unsigned)(dur / 60 % 60), (unsigned)(dur % 60));
    dstrbuf_add(b, tmp);
  }
  if (bitreader_get(br, 1, &v)) return -1;
  if (v) {
    dstrbuf_add(b, "/");
    if (dvb_string_append(sr, b)) return -1;
  }
  return b->buf ? 0 : -1;
}

int dvb_locator_decode_buf(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, dstrbuf_t *b) {
  uint64_t flag;
  if (bitreader_get(br, 1, &flag)) return -1;
  return flag ? locator_decode_optimized(br, sr, st, b) : dvb_string_append(sr, b);
}

int dvb_locator_decode_st(bitreader_t *br, strrepo_reader_t *sr, dvb_codec_state_t *st, char *out, size_t outcap) {
  dstrbuf_t b;
  dstrbuf_init(&b);
  return copy_out(&b, b.buf ? dvb_locator_decode_buf(br, sr, st, &b) : -1, out, outcap);
}

int dvb_locator_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) {
  dvb_codec_state_t st;
  memset(&st, 0, sizeof st);
  return dvb_locator_decode_st(br, sr, &st, out, outcap);
}

#define MJD_MIN 15079L /* 1900-03-01 */
#define MJD_MAX 88127L /* 2100-02-28 */
#define MS_PER_DAY 86400000ULL

static long mjd16_full(uint64_t v) {
  long full = (long)v;
  while (full < MJD_UNIX_EPOCH) full += 65536L;
  return full;
}

int dvb_datetime_encode_st(bitwriter_t *bw, dvb_codec_state_t *st, const char *iso8601, int published) {
  iso8601_t f;
  long total_minutes;
  long mjd_utc;
  long minute_of_day;
  int hour;
  int min;
  uint64_t ms;
  long frac;

  if (iso8601_split(iso8601, &f) || iso_fraction_ms(iso8601, &frac)) return -1;
  /* no offset: UTC (off_min 0) */
  total_minutes = iso8601_mjd_minutes(&f);
  mjd_utc = floor_div(total_minutes, 1440L);
  minute_of_day = total_minutes - mjd_utc * 1440L;
  if (mjd_utc < MJD_MIN || mjd_utc > MJD_MAX) return -1;
  hour = (int)(minute_of_day / 60);
  min = (int)(minute_of_day % 60);
  if (published && !f.s && !frac && mjd_utc >= MJD_UNIX_EPOCH && mjd_utc < MJD_UNIX_EPOCH + 65536L) {
    int reuse = st->have_date && st->mjd == mjd_utc;
    if (bitwriter_put(bw, 1, 2) || bitwriter_put(bw, !reuse, 1)) return -1; /* PublishedTime */
    if (!reuse && bitwriter_put(bw, (uint64_t)mjd_utc & 0xFFFFu, 16)) return -1;
    st->have_date = 1;
    st->mjd = mjd_utc;
    return bitwriter_put(bw, (uint64_t)minute_of_day, 11);
  }
  ms = (uint64_t)(hour * 3600 + min * 60 + f.s) * 1000ULL + (uint64_t)frac;
  if (bitwriter_put(bw, 0, 2)) return -1; /* dateTime_flag = 00, full precision */
  if (bitwriter_put(bw, (uint64_t)mjd_utc, 32)) return -1;
  return bitwriter_put(bw, ms, 32);
}

int dvb_datetime_encode(bitwriter_t *bw, const char *iso8601) { return dvb_datetime_encode_st(bw, NULL, iso8601, 0); }

/* ms: fraction, trailing zeros dropped */
static int fmt_iso(long mjd, long minute_of_day, int sec, unsigned ms, char *out, size_t outcap) {
  int n = iso8601_format(out, outcap, mjd - MJD_UNIX_EPOCH, minute_of_day * 60 + sec);
  char frac[16] = "";
  int m;
  if (n < 0) return -1;
  if (ms) {
    size_t k;
    snprintf(frac, sizeof frac, ".%03u", ms);
    k = strlen(frac);
    while (frac[k - 1] == '0') k--;
    frac[k] = '\0';
  }
  m = snprintf(out + n, outcap - (size_t)n, "%sZ", frac);
  return m < 0 || (size_t)m >= outcap - (size_t)n ? -1 : 0;
}

int dvb_datetime_decode_st(bitreader_t *br, dvb_codec_state_t *st, char *out, size_t outcap) {
  uint64_t flag;
  uint64_t mjd_v;
  uint64_t ms_v;

  if (bitreader_get(br, 2, &flag)) return -1;
  if (flag == 1) {
    uint64_t date_flag;
    uint64_t date;
    uint64_t minutes;
    if (bitreader_get(br, 1, &date_flag)) return -1;
    if (date_flag) {
      if (bitreader_get(br, 16, &date)) return -1;
      st->have_date = 1;
      st->mjd = mjd16_full(date);
    } else if (!st->have_date) {
      return -1;
    }
    if (bitreader_get(br, 11, &minutes) || minutes >= 1440) return -1;
    return fmt_iso(st->mjd, (long)minutes, 0, 0, out, outcap);
  }
  if (flag != 0) return -1;
  if (bitreader_get(br, 32, &mjd_v)) return -1;
  if (bitreader_get(br, 32, &ms_v)) return -1;
  if (mjd_v < (uint64_t)MJD_MIN || mjd_v > (uint64_t)MJD_MAX || ms_v >= MS_PER_DAY) return -1;
  return fmt_iso((long)mjd_v, (long)(ms_v / 60000ULL), (int)((ms_v / 1000ULL) % 60ULL), (unsigned)(ms_v % 1000ULL), out, outcap);
}

int dvb_datetime_decode(bitreader_t *br, char *out, size_t outcap) {
  dvb_codec_state_t st;
  memset(&st, 0, sizeof st);
  return dvb_datetime_decode_st(br, &st, out, outcap);
}

/* tables 70, 72 to 75: ClassificationSchemeID, then indexes of groups 2 to 5 */
static const char *const cs_id[] = {NULL, "urn:tva:metadata:cs:ActionTypeCS:2004", "urn:tva:metadata:cs:AtmosphereCS:2004", "urn:tva:metadata:cs:ContentAlertCS:2004", "urn:tva:metadata:cs:ContentCommercialCS:2004", "urn:tva:metadata:cs:ContentCS:2004", "urn:tva:metadata:cs:FormatCS:2004", "urn:tva:metadata:cs:HowRelatedCS:2004", "urn:tva:metadata:cs:IntendedAudienceCS:2004", "urn:tva:metadata:cs:IntentionCS:2004", "urn:tva:metadata:cs:LanguageCS:2004", "urn:tva:metadata:cs:MediaTypeCS:2004", "urn:tva:metadata:cs:OriginationCS:2004", "urn:mpeg:mpeg7:cs:RoleCS:2001", "urn:tva:metadata:cs:TVARoleCS:2004", "urn:tva:metadata:cs:AudioPurposeCS:2004", "urn:tva:metadata:cs:PurchaseTypeCS:2004", "urn:tva:metadata:cs:UnitTypeCS:2004"};
static const char *const cs_g2[] = {NULL, "urn:tva:metadata:cs:AtmosphereCS:2005", "urn:tva:metadata:cs:ContentAlertCS:2005", "urn:tva:metadata:cs:ContentCommercialCS:2005", "urn:tva:metadata:cs:ContentCS:2005", "urn:tva:metadata:cs:FormatCS:2005", "urn:tva:metadata:cs:HowRelatedCS:2005", "urn:tva:metadata:cs:IntendedAudienceCS:2005", "urn:tva:metadata:cs:IntentionCS:2005", "urn:tva:metadata:cs:MediaType:2005", "urn:tva:metadata:cs:OriginationCS:2005", "urn:tva:metadata:cs:TVARoleCS:2005"};
static const char *const cs_g3[] = {NULL, "urn:dvb:ipdc:esg:cs:ServiceTypeCS:2005", "urn:dvb:ipdc:esg:cs:ZappingSupportCS:2005"};
static const char *const cs_g4[] = {NULL, "urn:dvb:metadata:cs:AudioCodecCS:2007", "urn:dvb:metadata:cs:VideoCodecCS:2007"};
static const char *const cs_g5[] = {NULL, "urn:tva:metadata:cs:AudioPurposeCS:2007", "urn:tva:metadata:cs:CaptionCodingFormatCS:2007", "urn:tva:metadata:cs:ContentCS:2007", "urn:tva:metadata:cs:DerivationReasonTypeCS:2007", "urn:tva:metadata:cs:FormatCS:2007", "urn:tva:metadata:cs:HowRelatedCS:2007", "urn:tva:metadata:cs:UnitTypeCS:2007"};

#define NTAB(a) (sizeof(a) / sizeof *(a))

int dvb_controlledterm_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *href) { return dvb_controlledterm_encode_cs(bw, sw, NULL, href); }

int dvb_controlledterm_encode_cs(bitwriter_t *bw, strrepo_writer_t *sw, const dvb_cs_t *cs, const char *href) {
  const char *colon = cs && cs->compact && cs->rank ? strrchr(href, ':') : NULL;
  size_t n = colon ? (size_t)(colon - href) : 0;
  for (size_t id = 1; colon && id < NTAB(cs_id); id++) {
    char uri[128];
    uint64_t rank;
    if (strlen(cs_id[id]) != n || strncmp(cs_id[id], href, n)) continue;
    memcpy(uri, href, n);
    uri[n] = '\0';
    if (cs->rank(cs->ctx, uri, colon + 1, &rank)) break;
    return bitwriter_put(bw, 1, 1) || bitwriter_put(bw, 0, 1) || bitwriter_put(bw, id, 7) || bitwriter_put_vluimsbf8(bw, rank) ? -1 : 0;
  }
  return dvb_flagged_string_encode(bw, sw, href); /* encoding_flag = 0, string form */
}

int dvb_controlledterm_decode(bitreader_t *br, strrepo_reader_t *sr, char *out, size_t outcap) { return dvb_controlledterm_decode_cs(br, sr, NULL, out, outcap); }

/* ClassificationSchemeURI of group and index, NULL unknown. group 0: codec parameter list */
static const char *cs_uri(const dvb_cs_t *cs, uint64_t group, uint64_t idx) {
  const char *const *tab = NULL;
  size_t n = 0;
  switch (group) {
    case 2: tab = cs_g2; n = NTAB(cs_g2); break;
    case 3: tab = cs_g3; n = NTAB(cs_g3); break;
    case 4: tab = cs_g4; n = NTAB(cs_g4); break;
    case 5: tab = cs_g5; n = NTAB(cs_g5); break;
    default: break;
  }
  if (group == 0) return cs && cs->list && idx < cs->nlist ? cs->list[idx] : NULL;
  return tab && idx < n ? tab[idx] : NULL;
}

int dvb_controlledterm_decode_buf(bitreader_t *br, strrepo_reader_t *sr, const dvb_cs_t *cs, dstrbuf_t *b) {
  uint64_t encoding_flag;
  uint64_t grouping;
  uint64_t group = 0;
  uint64_t csid;
  uint64_t term;
  const char *uri;
  if (bitreader_get(br, 1, &encoding_flag)) return -1;
  if (!encoding_flag) return dvb_string_append(sr, b);
  if (bitreader_get(br, 1, &grouping)) return -1;
  if (!grouping) {
    if (bitreader_get(br, 7, &csid)) return -1;
  } else if (bitreader_get_vluimsbf5(br, &group) || bitreader_get_vluimsbf8(br, &csid)) {
    return -1;
  }
  if (bitreader_get_vluimsbf8(br, &term)) return -1;
  uri = NULL;
  if (grouping) uri = cs_uri(cs, group, csid);
  else if (csid < NTAB(cs_id)) uri = cs_id[csid];
  if (uri && cs && cs->term) {
    char name[256];
    if (cs->term(cs->ctx, uri, term, name, sizeof name)) return -1;
    dstrbuf_add(b, uri);
    dstrbuf_add(b, ":");
    dstrbuf_add(b, name);
    return b->buf ? 0 : -1;
  }
  dstrbuf_add(b, "dvbcs:");
  dstrbuf_add_u64(b, group);
  dstrbuf_add(b, ":");
  dstrbuf_add_u64(b, csid);
  dstrbuf_add(b, ":");
  dstrbuf_add_u64(b, term);
  return b->buf ? 0 : -1;
}

int dvb_controlledterm_decode_cs(bitreader_t *br, strrepo_reader_t *sr, const dvb_cs_t *cs, char *out, size_t outcap) {
  dstrbuf_t b;
  dstrbuf_init(&b);
  return copy_out(&b, b.buf ? dvb_controlledterm_decode_buf(br, sr, cs, &b) : -1, out, outcap);
}

int dvb_duration_encode(bitwriter_t *bw, strrepo_writer_t *sw, const char *s, int compact) {
  unsigned h;
  unsigned m;
  int used = 0;
  char back[32];
  if (compact && sscanf(s, "PT%uH%uM%n", &h, &m, &used) == 2 && !s[used] && m < 60 && h < 35 && h * 60 + m < 2048) {
    snprintf(back, sizeof back, "PT%uH%uM", h, m);
    if (!strcmp(back, s)) return bitwriter_put(bw, 1, 1) || bitwriter_put(bw, h * 60 + m, 11) ? -1 : 0;
  }
  return dvb_flagged_string_encode(bw, sw, s);
}
