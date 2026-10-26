/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/base64.h"
#include "lib/bim/codec/decode/priv.h"

int dec_add_n(dstrbuf_t *b, const char *s, size_t n) {
  dstrbuf_add_n(b, s, n);
  return b->buf ? 0 : -1;
}

int dec_add(dstrbuf_t *b, const char *s) { return dec_add_n(b, s, strlen(s)); }

/* TS 102 822-3-2 4.4.2.4.4: string might span chunks */
int dec_zstring(dec_t *d, dstrbuf_t *b) {
  size_t zcap = d->opts && d->opts->zbuf_size ? d->opts->zbuf_size : ZLIB_BUF_MAX;
  for (;;) {
    const unsigned char *z;
    size_t n;
    if (d->zpos == d->zlen) {
      unsigned char *in;
      uint64_t clen;
      size_t zl = zcap;
      int rc;
      if (!d->inflate) return -1;
      if (bitreader_get_vluimsbf8(d->br, &clen)) return -1;
      if (!clen) return -1;
      if (clen > ZLIB_CHUNK_MAX(zcap)) return -1;
      if (!d->zbuf) d->zbuf = malloc(zcap);
      if (!d->zbuf) return -1;
      if (!(in = malloc((size_t)clen))) return -1;
      rc = bitreader_get_bytes(d->br, in, (size_t)clen) || d->inflate(in, (size_t)clen, d->zbuf, &zl) || !zl;
      free(in);
      if (rc) return -1;
      d->zlen = zl;
      d->zpos = 0;
    }
    n = d->zlen - d->zpos;
    z = memchr(d->zbuf + d->zpos, 0, n);
    if (z) n = (size_t)(z - (d->zbuf + d->zpos));
    if (dec_add_n(b, (const char *)d->zbuf + d->zpos, n)) return -1;
    d->zpos += n;
    if (z) {
      d->zpos++;
      return 0;
    }
  }
}

/* vluimsbf5 bit count + bits, ISO 23001-1 7.5.4.1. hex or base64 text */
static int binary_render(dec_t *d, int hex, dstrbuf_t *b) {
  static const char digits[] = "0123456789ABCDEF";
  unsigned char *raw;
  char *txt;
  uint64_t nbits;
  size_t n;
  int rc = -1;
  if (bitreader_get_vluimsbf5(d->br, &nbits) || nbits % 8 || nbits / 8 > bitreader_bits_left(d->br) / 8) return -1;
  n = (size_t)(nbits / 8);
  raw = malloc(n ? n : 1);
  txt = malloc((hex ? n * 2 : base64_encoded_len(n)) + 1);
  if (raw && txt && !bitreader_get_bytes(d->br, raw, n)) {
    if (hex) {
      for (size_t i = 0; i < n; i++) {
        txt[2 * i] = digits[raw[i] >> 4];
        txt[2 * i + 1] = digits[raw[i] & 15];
      }
      txt[2 * n] = '\0';
    } else {
      base64_encode(raw, n, txt);
    }
    rc = dec_add(b, txt);
  }
  free(raw);
  free(txt);
  return rc ? -1 : 0;
}

/* vluimsbf5 byte count + UTF-8, ISO 23001-1 7.5.4.1 */
static int utf8_render(dec_t *d, dstrbuf_t *b) {
  uint64_t n;
  char *s;
  int rc;
  if (bitreader_get_vluimsbf5(d->br, &n)) return -1;
  if (n > bitreader_bits_left(d->br) / 8) return -1;
  s = malloc((size_t)n + 1);
  if (!s) return -1;
  rc = bitreader_get_bytes(d->br, (unsigned char *)s, (size_t)n) || memchr(s, '\0', (size_t)n) || dec_add_n(b, s, (size_t)n);
  free(s);
  return rc ? -1 : 0;
}

static int get_flag(dec_t *d, int *f) {
  uint64_t v;
  if (bitreader_get(d->br, 1, &v)) return -1;
  *f = (int)v;
  return 0;
}

/* 0 if f not coded */
static int rflag(dec_t *d, unsigned mask, int f, int *v) {
  *v = 0;
  return M7_HAS(mask, f) ? get_flag(d, v) : 0;
}

static int get_u(dec_t *d, int bits, unsigned *out) {
  uint64_t v;
  if (bitreader_get(d->br, bits, &v)) return -1;
  *out = (unsigned)v;
  return 0;
}

/* ISO 15938-1:2002 8.5.4.2.5 */
static int timepoint_render(dec_t *d, const bim_simple_t *s, char *out, size_t cap) {
  m7_timepoint_t t;
  unsigned m = m7_timepoint_flags(s, d->compat);
  unsigned tzh;
  memset(&t, 0, sizeof t);
  if (d->compat & BIM_COMPAT_M7_FLAGS) {
    if (rflag(d, m, M7T_NEG, &t.neg) || rflag(d, m, M7T_DATE, &t.date) || (t.date && (rflag(d, m, M7T_MONTH, &t.has_month) || (t.has_month && rflag(d, m, M7T_DAY, &t.has_day))))) return -1;
  } else if (get_flag(d, &t.date) || (t.date && (get_flag(d, &t.neg) || get_flag(d, &t.has_month) || (t.has_month && get_flag(d, &t.has_day))))) {
    return -1;
  }
  if (rflag(d, m, M7T_TIME, &t.time) ||
      (t.time && (rflag(d, m, M7T_MIN, &t.has_min) ||
                  (t.has_min && (rflag(d, m, M7T_SEC, &t.has_sec) || (t.has_sec && (rflag(d, m, M7T_N, &t.has_n) || (t.has_n && rflag(d, m, M7T_FF, &t.has_ff)))))))))
    return -1;
  if (rflag(d, m, M7T_FRAC, &t.has_frac) || rflag(d, m, M7T_TZ, &t.tz)) return -1;
  if (t.date && (bitreader_get_vluimsbf5(d->br, &t.year) || (t.has_month && get_u(d, 4, &t.month)) || (t.has_day && get_u(d, 5, &t.day)))) return -1;
  if (d->compat & BIM_COMPAT_M7_FLAGS) {
    t.month += (unsigned)t.has_month;
    t.day += (unsigned)t.has_day;
  }
  if (t.time && (get_u(d, 5, &t.hour) || (t.has_min && get_u(d, 6, &t.min)) || (t.has_sec && get_u(d, 6, &t.sec)))) return -1;
  if (t.has_frac && bitreader_get_vluimsbf5(d->br, &t.frac)) return -1;
  if (t.has_n && (!t.has_frac || !t.frac || bitreader_get(d->br, bim_width(t.frac), &t.n) || (t.has_ff && get_u(d, 7, &t.ff)))) return -1;
  if (t.tz) {
    if (get_u(d, 5, &tzh) || get_u(d, 2, &t.tzm)) return -1;
    t.tzh = (int)tzh - 15;
    t.tzm *= 15;
  }
  return m7_timepoint_render(&t, out, cap);
}

/* ISO 15938-1:2002 8.5.4.2.6 */
static int duration_render(dec_t *d, const bim_simple_t *s, char *out, size_t cap) {
  m7_duration_t m;
  unsigned fl = m7_duration_flags(s, d->compat);
  unsigned tzh;
  memset(&m, 0, sizeof m);
  if (rflag(d, fl, M7D_NEG, &m.neg) || rflag(d, fl, M7D_D, &m.has_d) || rflag(d, fl, M7D_T, &m.t)) return -1;
  for (int u = 0; m.t && u < 4; u++)
    if (rflag(d, fl, M7D_H + u, &m.has[u])) return -1;
  if ((m.t && rflag(d, fl, M7D_FF, &m.has_ff)) || rflag(d, fl, M7D_FRAC, &m.has_frac) || rflag(d, fl, M7D_TZ, &m.tz)) return -1;
  if (m.has_d && bitreader_get_vluimsbf5(d->br, &m.d)) return -1;
  for (int u = 0; m.t && u < 3; u++) {
    int fixed = u ? m.has[u - 1] : m.has_d;
    int width = u ? 6 : 5;
    if (m.has[u] && (fixed ? bitreader_get(d->br, width, &m.v[u]) : bitreader_get_vluimsbf5(d->br, &m.v[u]))) return -1;
  }
  if (m.has_frac && bitreader_get_vluimsbf5(d->br, &m.frac)) return -1;
  if (m.has[3]) {
    if (!m.has[2]) {
      if (bitreader_get_vluimsbf5(d->br, &m.v[3])) return -1;
    } else if (!m.has_frac || !m.frac || bitreader_get(d->br, bim_width(m.frac), &m.v[3])) {
      return -1;
    }
  }
  if (m.has_ff && get_u(d, 7, &m.ff)) return -1;
  if (m.tz) {
    if (get_u(d, 6, &tzh) || get_u(d, 2, &m.tzm)) return -1;
    m.tzh = (int)tzh - 31;
    m.tzm *= 15;
  }
  return m7_duration_render(&m, out, cap);
}

/* XML Schema Part 2 3.2.4, 3.2.5 lexical forms of special values */
void dec_float_render(double x, int prec, char *buf, size_t cap) {
  if (isnan(x)) snprintf(buf, cap, "NaN");
  else if (isinf(x)) snprintf(buf, cap, "%s", x < 0 ? "-INF" : "INF");
  else snprintf(buf, cap, "%.*g", prec, x);
}

/* ISO 23001-1 7.5.4.2.3 */
int dec_list_count(dec_t *d, const bim_simple_t *s, uint64_t *n) {
  int w = s->unbounded ? -1 : range_bits(&s->range);
  if (w < 0 ? bitreader_get_vluimsbf5(d->br, n) : bitreader_get(d->br, w, n)) return -1;
  if ((!s->unbounded && *n > s->range.span) || *n > UINT64_MAX - (uint64_t)s->range.min) return -1;
  *n += (uint64_t)s->range.min;
  return 0;
}

static int list_render(dec_t *d, const bim_simple_t *s, dstrbuf_t *b) {
  uint64_t n;
  if (dec_list_count(d, s, &n)) return -1;
  for (uint64_t i = 0; i < n; i++) {
    size_t bits = bitreader_bits_left(d->br);
    if ((i && dec_add(b, " ")) || dec_text(d, s->item, b)) return -1;
    /* zero-bit items: count bounded like nodes */
    if (bits == bitreader_bits_left(d->br) && n > MAX_NODES) return -1;
  }
  return 0;
}

int dec_plain(dec_t *d, const bim_simple_t *s, dstrbuf_t *b) {
  char t[160];
  uint64_t v;
  if (!s) return -1;
  switch (s->codec) {
    case BIM_UTF8: return utf8_render(d, b);
    case BIM_BOOLEAN:
      if (bitreader_get(d->br, 1, &v)) return -1;
      return dec_add(b, v ? "true" : "false");
    case BIM_FLOAT: {
      uint32_t bits;
      float f;
      if (bitreader_get(d->br, 32, &v)) return -1;
      bits = (uint32_t)v;
      memcpy(&f, &bits, sizeof f);
      dec_float_render((double)f, 9, t, sizeof t);
      return dec_add(b, t);
    }
    case BIM_DOUBLE: {
      double x;
      if (bitreader_get(d->br, 64, &v)) return -1;
      memcpy(&x, &v, sizeof x);
      dec_float_render(x, 17, t, sizeof t);
      return dec_add(b, t);
    }
    case BIM_HEXBIN: return binary_render(d, 1, b);
    case BIM_BASE64: return binary_render(d, 0, b);
    case BIM_RANGE: {
      int w = range_bits(&s->range);
      if (w < 0 ? bitreader_get_vluimsbf5(d->br, &v) : bitreader_get(d->br, w, &v)) return -1;
      if (v > s->range.span) return -1;
      if (s->range.min >= 0) snprintf(t, sizeof t, "%llu", (unsigned long long)((uint64_t)s->range.min + v));
      else snprintf(t, sizeof t, "%lld", (long long)((uint64_t)s->range.min + v));
      return dec_add(b, t);
    }
    case BIM_SVLU5: {
      uint64_t sign;
      if (bitreader_get(d->br, 1, &sign) || bitreader_get_vluimsbf5(d->br, &v)) return -1;
      t[0] = '-';
      u64_to_dec(t + (sign ? 1 : 0), v);
      return dec_add(b, t);
    }
    case BIM_ENUM:
      if (bitreader_get(d->br, bim_width((uint64_t)s->nenums), &v) || v >= (uint64_t)s->nenums) return -1;
      return dec_add(b, s->enums[v]);
    case BIM_LIST: return list_render(d, s, b);
    case BIM_UNION:
      if (bitreader_get(d->br, bim_width((uint64_t)s->nmembers), &v) || v >= (uint64_t)s->nmembers) return -1;
      return dec_text(d, s->members[v], b);
    case BIM_STRING: return dvb_string_append(d->sr, b);
    case BIM_ZSTRING: return dec_zstring(d, b);
    case BIM_LOCATOR: return dvb_locator_decode_buf(d->br, d->sr, &d->st, b);
    case BIM_DATETIME:
    case BIM_PUBTIME: return dvb_datetime_decode_st(d->br, &d->st, t, sizeof t) || dec_add(b, t);
    case BIM_DURATION:
      if (bitreader_get(d->br, 1, &v)) return -1;
      if (!v) return dvb_string_append(d->sr, b);
      if (bitreader_get(d->br, 11, &v)) return -1;
      snprintf(t, sizeof t, "PT%lluH%lluM", (unsigned long long)(v / 60), (unsigned long long)(v % 60));
      return dec_add(b, t);
    case BIM_TERMREF: return d->sr ? dvb_controlledterm_decode_buf(d->br, d->sr, d->st.cs, b) : -1;
    case BIM_TVA_DATETIME: return tva_datetime_decode(d->br, t, sizeof t) || dec_add(b, t);
    case BIM_TVA_DATE: return tva_date_decode(d->br, t, sizeof t) || dec_add(b, t);
    case BIM_M7_TIMEPOINT: return timepoint_render(d, s, t, sizeof t) || dec_add(b, t);
    case BIM_M7_DURATION: return duration_render(d, s, t, sizeof t) || dec_add(b, t);
    default: return -1;
  }
}

/* 7.4.7: advanced optimised decoder mapped to type or base, else default */
int dec_text(dec_t *d, const bim_simple_t *s, dstrbuf_t *b) {
  const bim_aod_map_t *m = d->opts && d->opts->aod && s ? bim_aod_find(d->opts->aod, s) : NULL;
  return m ? dec_aod(d, s, m, b) : dec_plain(d, s, b);
}

int dec_value(dec_t *d, const bim_simple_t *s, char **out) {
  if (!d->vb.buf) dstrbuf_init(&d->vb);
  if (!d->vb.buf) return -1;
  d->vb.len = 0;
  d->vb.buf[0] = '\0';
  if (dec_text(d, s, &d->vb)) return -1;
  if (!d->vb.buf) return -1;
  *out = malloc(d->vb.len + 1);
  if (!*out) return -1;
  memcpy(*out, d->vb.buf, d->vb.len + 1);
  return 0;
}

void dec_done(dec_t *d) {
  free(d->zbuf);
  d->zbuf = NULL;
  free(d->vb.buf);
  memset(&d->vb, 0, sizeof d->vb);
}

void dec_init(dec_t *d, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr) {
  static const bim_schema_t one = {.known = 1};
  memset(d, 0, sizeof *d);
  d->br = br;
  d->sr = sr;
  d->opts = opts;
  d->schemas = opts && opts->nschemas ? opts->schemas : &one;
  d->nschemas = opts && opts->nschemas ? opts->nschemas : 1;
  d->inflate = opts ? opts->inflate : NULL;
  d->mpegb = opts && opts->mpegb;
  d->compat = opts ? opts->compat : 0;
  d->hot = 1;
  d->nofragref = 1;
  d->st.cs = opts ? opts->cs : NULL;
}

int bim_decode_simple(const bim_simple_t *s, const bim_decode_opts_t *opts, bitreader_t *br, strrepo_reader_t *sr, char **out) {
  dec_t d;
  int rc;
  dec_init(&d, opts, br, sr);
  *out = NULL;
  rc = dec_value(&d, s, out);
  dec_done(&d);
  return rc;
}
