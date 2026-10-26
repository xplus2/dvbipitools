/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/base64.h"
#include "lib/bim/aod.h"
#include "lib/bim/codec/encode/priv.h"

int enc_bit(bitwriter_t *bw, int v) { return bitwriter_put(bw, v ? 1 : 0, 1); }

const char *enc_skip_ws(const char *s) {
  while (isspace((unsigned char)*s)) s++;
  return s;
}

static int parse_uint(const char *v, uint64_t *out) {
  char *end;
  unsigned long long n;
  v = enc_skip_ws(v);
  if (*v < '0' || *v > '9') return -1;
  errno = 0;
  n = strtoull(v, &end, 10);
  if (errno || *enc_skip_ws(end)) return -1;
  *out = n;
  return 0;
}

/* XML Schema Part 2 3.3.13: optional sign, digits */
static int parse_int(const char *v, int *neg, uint64_t *out) {
  v = enc_skip_ws(v);
  *neg = *v == '-';
  if (*v == '-' || *v == '+') v++;
  return *v < '0' || *v > '9' ? -1 : parse_uint(v, out);
}

/* XML Schema Part 2 3.2.4, 3.2.5 lexical space: decimal mantissa, optional exponent, INF, -INF, NaN */
int enc_parse_double(const char *v, double *out) {
  const char *p;
  const char *e;
  int digits = 0;
  char *end;
  v = enc_skip_ws(v);
  for (e = v + strlen(v); e > v && isspace((unsigned char)e[-1]); e--) {
  }
  if (e - v == 3 && !strncmp(v, "INF", 3)) return *out = HUGE_VAL, 0;
  if (e - v == 4 && !strncmp(v, "-INF", 4)) return *out = -HUGE_VAL, 0;
  if (e - v == 3 && !strncmp(v, "NaN", 3)) return *out = NAN, 0;
  p = v + (*v == '+' || *v == '-');
  for (; p < e && isdigit((unsigned char)*p); p++) digits++;
  if (p < e && *p == '.')
    for (p++; p < e && isdigit((unsigned char)*p); p++) digits++;
  if (!digits) return -1;
  if (p < e && (*p == 'e' || *p == 'E')) {
    p++;
    if (p < e && (*p == '+' || *p == '-')) p++;
    if (p >= e || !isdigit((unsigned char)*p)) return -1;
    while (p < e && isdigit((unsigned char)*p)) p++;
  }
  if (p != e) return -1;
  *out = strtod(v, &end);
  return 0;
}

static int parse_boolean(const char *v, int *out) {
  size_t n;
  v = enc_skip_ws(v);
  n = strlen(v);
  while (n && isspace((unsigned char)v[n - 1])) n--;
  if ((n == 4 && !strncmp(v, "true", 4)) || (n == 1 && *v == '1')) *out = 1;
  else if ((n == 5 && !strncmp(v, "false", 5)) || (n == 1 && *v == '0')) *out = 0;
  else return -1;
  return 0;
}

static int enum_index(const bim_simple_t *s, const char *v) {
  for (int i = 0; i < s->nenums; i++)
    if (!strcmp(s->enums[i], v)) return i;
  return -1;
}

static int encode_range(const bim_range_t *r, const char *v, bitwriter_t *bw) {
  uint64_t u;
  uint64_t off;
  uint64_t mag = r->min < 0 ? (uint64_t)(-(r->min + 1)) + 1 : 0; /* |min| if negative */
  int neg;
  int w = range_bits(r);
  if (parse_int(v, &neg, &u)) return -1;
  if (neg && u) {
    if (u > mag) return -1;
    off = mag - u;
  } else if (r->min >= 0) {
    if (u < (uint64_t)r->min) return -1;
    off = u - (uint64_t)r->min;
  } else {
    if (u > UINT64_MAX - mag) return -1;
    off = u + mag;
  }
  if (off > r->span) return -1;
  return w < 0 ? bitwriter_put_vluimsbf5(bw, off) : bitwriter_put(bw, off, w);
}

static int put_raw(bitwriter_t *bw, const unsigned char *b, size_t n, int bits) {
  if (bits ? bitwriter_put_vluimsbf5(bw, (uint64_t)n * 8) : bitwriter_put_vluimsbf5(bw, n)) return -1;
  for (size_t i = 0; i < n; i++)
    if (bitwriter_put(bw, b[i], 8)) return -1;
  return 0;
}

static int hex_nibble(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = tolower(c);
  return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int encode_hex(const char *v, bitwriter_t *bw) {
  unsigned char *raw;
  size_t n = 0;
  size_t len;
  int rc;
  v = enc_skip_ws(v);
  len = strlen(v);
  while (len && isspace((unsigned char)v[len - 1])) len--;
  if (len % 2 || !(raw = malloc(len / 2 + 1))) return -1;
  for (size_t i = 0; i < len; i += 2) {
    int hi = hex_nibble((unsigned char)v[i]);
    int lo = hex_nibble((unsigned char)v[i + 1]);
    if (hi < 0 || lo < 0) {
      free(raw);
      return -1;
    }
    raw[n++] = (unsigned char)(hi << 4 | lo);
  }
  rc = put_raw(bw, raw, n, 1);
  free(raw);
  return rc;
}

/* *raw mallocd, *n octets */
static int base64_raw(const char *v, unsigned char **raw, size_t *n) {
  size_t cap = strlen(v) / 4 * 3 + 3;
  if (!(*raw = malloc(cap))) return -1;
  if (!base64_decode(v, *raw, cap, n)) return 0;
  free(*raw);
  return -1;
}

static int encode_base64(const char *v, bitwriter_t *bw) {
  unsigned char *raw;
  size_t n;
  int rc;
  if (base64_raw(v, &raw, &n)) return -1;
  rc = put_raw(bw, raw, n, 1);
  free(raw);
  return rc;
}

/* ISO 23001-1 7.5.4.2.3 */
static int encode_list(enc_t *e, const bim_simple_t *s, const char *v) {
  uint64_t n = 0;
  int w = s->unbounded ? -1 : range_bits(&s->range);
  for (const char *p = enc_skip_ws(v); *p; p = enc_skip_ws(p + strcspn(p, " \t\r\n"))) n++;
  if (s->range.min > 0 && n < (uint64_t)s->range.min) return -1;
  n -= (uint64_t)s->range.min;
  if (!s->unbounded && n > s->range.span) return -1;
  if (w < 0 ? bitwriter_put_vluimsbf5(e->bw, n) : bitwriter_put(e->bw, n, w)) return -1;
  for (const char *p = enc_skip_ws(v); *p; p = enc_skip_ws(p)) {
    size_t len = strcspn(p, " \t\r\n");
    char *item = strndup(p, len);
    int rc = !item || enc_text(e, s->item, item);
    free(item);
    if (rc) return -1;
    p += len;
  }
  return 0;
}

/* XML Schema Part 2 4.3.1 .. 4.3.4: length in characters, octets for binary */
static int facets_ok(const bim_simple_t *s, const char *v) {
  for (int i = 0; i < s->npatterns; i++)
    if (s->match(s->patterns[i], v) != 1) return 0;
  if (s->has_length) {
    uint64_t n = 0;
    if (s->codec == BIM_HEXBIN) n = strlen(enc_skip_ws(v)) / 2;
    else if (s->codec == BIM_BASE64) {
      unsigned char *raw;
      size_t len;
      if (base64_raw(v, &raw, &len)) return 0;
      free(raw);
      n = len;
    } else {
      for (const char *p = v; *p; p++) n += ((unsigned char)*p & 0xC0) != 0x80;
    }
    if (n < s->minlen || n > s->maxlen) return 0;
  }
  return 1;
}

/* first member whose facets validate v, XML Schema Part 2 2.5.1.3. trial: zlib plan, decoder counters untouched */
static int union_member(const enc_t *e, const bim_simple_t *s, const char *v) {
  bim_encode_opts_t o = *e->o;
  o.aod = NULL;
  for (int i = 0; i < s->nmembers; i++) {
    bitwriter_t bw;
    strrepo_writer_t sw;
    enc_t t = *e;
    int rc;
    bitwriter_init(&bw);
    if (strrepo_writer_init(&sw)) return -1;
    t.o = &o;
    t.bw = &bw;
    t.sw = &sw;
    t.resync = 0;
    t.zmode = 0;
    t.zp = NULL;
    rc = facets_ok(s->members[i], v) ? enc_plain(&t, s->members[i], v) : -1;
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
    if (!rc) return i;
  }
  return -1;
}

int enc_text(enc_t *e, const bim_simple_t *s, const char *v) {
  const bim_aod_map_t *m = e->o->aod && s ? bim_aod_find(e->o->aod, s) : NULL;
  int armed = e->sw && e->resync;
  int rc;
  if (armed) e->sw->resync = e->bw;
  rc = m ? enc_aod(e, s, m, v) : enc_plain(e, s, v);
  if (armed && !e->sw->resync) e->resync = 0;
  return rc;
}

int enc_plain(enc_t *e, const bim_simple_t *s, const char *v) {
  bitwriter_t *bw = e->bw;
  strrepo_writer_t *sw = e->sw;
  uint64_t u;
  int b;
  if (!s) return -1;
  switch (s->codec) {
    case BIM_UTF8: return put_raw(bw, (const unsigned char *)v, strlen(v), 0);
    case BIM_ZSTRING: return enc_zstring(e, v);
    case BIM_BOOLEAN: return parse_boolean(v, &b) ? -1 : enc_bit(bw, b);
    case BIM_FLOAT: {
      double d;
      float f;
      uint32_t bits;
      if (enc_parse_double(v, &d)) return -1;
      f = (float)d;
      memcpy(&bits, &f, sizeof bits);
      return bitwriter_put(bw, bits, 32);
    }
    case BIM_DOUBLE: {
      double d;
      if (enc_parse_double(v, &d)) return -1;
      memcpy(&u, &d, sizeof u);
      return bitwriter_put(bw, u, 64);
    }
    case BIM_HEXBIN: return encode_hex(v, bw);
    case BIM_BASE64: return encode_base64(v, bw);
    case BIM_RANGE: return encode_range(&s->range, v, bw);
    case BIM_SVLU5:
      if (parse_int(v, &b, &u) || enc_bit(bw, b)) return -1;
      return bitwriter_put_vluimsbf5(bw, u);
    case BIM_ENUM:
      b = enum_index(s, v);
      return b < 0 ? -1 : bitwriter_put(bw, (uint64_t)b, bim_width((uint64_t)s->nenums));
    case BIM_LIST: return encode_list(e, s, v);
    case BIM_UNION:
      b = union_member(e, s, v);
      if (b < 0 || bitwriter_put(bw, (uint64_t)b, bim_width((uint64_t)s->nmembers))) return -1;
      return enc_text(e, s->members[b], v);
    case BIM_STRING: return dvb_string_encode(sw, v);
    case BIM_LOCATOR: return dvb_locator_encode_st(bw, sw, &e->st, v, e->o->dvb_locator);
    case BIM_DATETIME: return e->o->dvb_pubtime ? dvb_datetime_encode_st(bw, &e->st, v, 1) : dvb_datetime_encode(bw, v);
    case BIM_PUBTIME: return dvb_datetime_encode_st(bw, &e->st, v, 1);
    case BIM_DURATION: return dvb_duration_encode(bw, sw, v, e->o->dvb_duration);
    case BIM_TERMREF: return dvb_controlledterm_encode_cs(bw, sw, e->st.cs, v);
    case BIM_TVA_DATETIME: return tva_datetime_encode(bw, v);
    case BIM_TVA_DATE: return tva_date_encode(bw, v);
    case BIM_M7_TIMEPOINT: return m7_timepoint_encode(v, s, e->o->compat, bw);
    case BIM_M7_DURATION: return m7_duration_encode(v, s, e->o->compat, bw);
    default: return -1;
  }
}
