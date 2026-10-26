/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "lib/bim/codec/codec_int.h"

#define addf(b, ...) \
  ({ \
    char tmp_[64]; \
    snprintf(tmp_, sizeof tmp_, __VA_ARGS__); \
    sbuf_add((b), tmp_); \
  })

/* digits in [*p, end), at least one. advances */
static int take_num(const char **p, const char *end, uint64_t *v, int *ndig) {
  uint64_t x = 0;
  int n = 0;
  while (*p < end && **p >= '0' && **p <= '9') {
    if (x > (UINT64_MAX - (uint64_t)(**p - '0')) / 10) return -1;
    x = x * 10 + (uint64_t)(**p - '0');
    (*p)++;
    n++;
  }
  *v = x;
  if (ndig) *ndig = n;
  return n ? 0 : -1;
}

static int take2(const char **p, const char *end, unsigned *v) {
  uint64_t x;
  int n;
  if (take_num(p, end, &x, &n) || n != 2) return -1;
  *v = (unsigned)x;
  return 0;
}

/* (+|-)hh:mm at [p, p + 6). -00:mm with mm not 0: sign not codable, refused */
static int take_tz(const char *p, int *h, unsigned *m) {
  const char *q = p + 1;
  unsigned hh;
  if ((*p != '+' && *p != '-')) return -1;
  if (take2(&q, p + 3, &hh)) return -1;
  if (*q++ != ':') return -1;
  if (take2(&q, p + 6, m)) return -1;
  if ((*p == '-' && !hh && *m)) return -1;
  *h = *p == '-' ? -(int)hh : (int)hh;
  return 0;
}

static int ends_tz(const char *s, size_t len) {
  int h;
  unsigned m;
  return len >= 6 && !take_tz(s + len - 6, &h, &m);
}

int m7_timepoint_parse(const char *s, m7_timepoint_t *t) {
  size_t len = strlen(s);
  const char *end = s + len;
  const char *p = s;
  const char *tp;
  const char *fp;
  memset(t, 0, sizeof *t);
  if (ends_tz(s, len)) {
    t->tz = 1;
    take_tz(end - 6, &t->tzh, &t->tzm);
    end -= 6;
  }
  fp = memchr(p, 'F', (size_t)(end - p));
  if (fp) {
    const char *q = fp + 1;
    if (take_num(&q, end, &t->frac, NULL) || q != end) return -1;
    t->has_frac = 1;
    end = fp;
  }
  tp = memchr(p, 'T', (size_t)(end - p));
  if ((tp ? tp : end) > p) {
    t->date = 1;
    if (*p == '-') {
      t->neg = 1;
      p++;
    }
    if (take_num(&p, tp ? tp : end, &t->year, NULL)) return -1;
    if (p < (tp ? tp : end)) {
      if (*p++ != '-' || take2(&p, tp ? tp : end, &t->month)) return -1;
      t->has_month = 1;
      if (p < (tp ? tp : end)) {
        if (*p++ != '-' || take2(&p, tp ? tp : end, &t->day)) return -1;
        t->has_day = 1;
      }
    }
    if (p != (tp ? tp : end)) return -1;
  }
  if (tp) {
    p = tp + 1;
    t->time = 1;
    if (take2(&p, end, &t->hour)) return -1;
    if (p < end && *p == ':') {
      p++;
      t->has_min = 1;
      if (take2(&p, end, &t->min)) return -1;
      if (p < end && *p == ':') {
        p++;
        t->has_sec = 1;
        if (take2(&p, end, &t->sec)) return -1;
        if (p < end && *p == ':') {
          p++;
          t->has_n = 1;
          if (take_num(&p, end, &t->n, NULL)) return -1;
          if (p < end && *p == '.') {
            p++;
            t->has_ff = 1;
            if (take2(&p, end, &t->ff)) return -1;
          }
        }
      }
    }
    if (p != end) return -1;
  }
  return 0;
}

int m7_timepoint_render(const m7_timepoint_t *t, char *out, size_t cap) {
  sbuf_t b;
  char num[32];
  sbuf_init(&b, out, cap);
  if (t->date) {
    if (t->neg) sbuf_add(&b, "-");
    snprintf(num, sizeof num, "%llu", (unsigned long long)t->year);
    sbuf_add(&b, num);
    if (t->has_month) addf(&b, "-%02u", t->month);
    if (t->has_day) addf(&b, "-%02u", t->day);
  }
  if (t->time) {
    addf(&b, "T%02u", t->hour);
    if (t->has_min) addf(&b, ":%02u", t->min);
    if (t->has_sec) addf(&b, ":%02u", t->sec);
    if (t->has_n) addf(&b, ":%llu", (unsigned long long)t->n);
    if (t->has_ff) addf(&b, ".%02u", t->ff);
  }
  if (t->has_frac) addf(&b, "F%llu", (unsigned long long)t->frac);
  if (t->tz) addf(&b, "%c%02d:%02u", t->tzh < 0 ? '-' : '+', t->tzh < 0 ? -t->tzh : t->tzh, t->tzm);
  return b.truncated ? -1 : 0;
}

int m7_duration_render(const m7_duration_t *d, char *out, size_t cap) {
  static const char units[] = "HMSN";
  sbuf_t b;
  sbuf_init(&b, out, cap);
  sbuf_add(&b, d->neg ? "-P" : "P");
  if (d->has_d) addf(&b, "%lluD", (unsigned long long)d->d);
  if (d->t) {
    sbuf_add(&b, "T");
    for (int u = 0; u < 4; u++)
      if (d->has[u]) addf(&b, "%llu%c", (unsigned long long)d->v[u], units[u]);
    if (d->has_ff) addf(&b, "%02uf", d->ff);
  }
  if (d->has_frac) addf(&b, "%lluF", (unsigned long long)d->frac);
  if (d->tz) addf(&b, "%c%02d:%02uZ", d->tzh < 0 ? '-' : '+', d->tzh < 0 ? -d->tzh : d->tzh, d->tzm);
  return b.truncated ? -1 : 0;
}

static int bit(bitwriter_t *bw, int v) { return bitwriter_put(bw, v ? 1 : 0, 1); }

static int tz_ok(int h, unsigned m, int lo, int hi) { return h >= lo && h <= hi && m % 15 == 0 && m <= 45; }

int m7_duration_parse(const char *s, m7_duration_t *d) {
  size_t len = strlen(s);
  const char *end = s + len;
  const char *p = s;
  uint64_t x;
  memset(d, 0, sizeof *d);
  if (len >= 7 && end[-1] == 'Z' && ends_tz(s, len - 1)) {
    d->tz = 1;
    take_tz(end - 7, &d->tzh, &d->tzm);
    end -= 7;
  }
  if (p < end && *p == '-') {
    d->neg = 1;
    p++;
  }
  if (p >= end) return -1;
  if (*p++ != 'P') return -1;
  if (p < end && *p >= '0' && *p <= '9') {
    const char *q = p;
    if (take_num(&q, end, &x, NULL)) return -1;
    if (q < end && *q == 'D') {
      d->has_d = 1;
      d->d = x;
      p = q + 1;
    }
  }
  if (p < end && *p == 'T') {
    static const char units[] = "HMSN";
    p++;
    d->t = 1;
    for (int u = 0; u < 4; u++) {
      const char *q = p;
      if (take_num(&q, end, &x, NULL) || q >= end || *q != units[u]) continue;
      d->has[u] = 1;
      d->v[u] = x;
      p = q + 1;
    }
    if (end - p >= 3 && p[2] == 'f') {
      const char *q = p;
      if (take2(&q, end, &d->ff)) return -1;
      d->has_ff = 1;
      p = q + 1;
    }
  }
  if (p < end) {
    if (take_num(&p, end, &d->frac, NULL)) return -1;
    if (p >= end) return -1;
    if (*p++ != 'F') return -1;
    d->has_frac = 1;
  }
  return p == end ? 0 : -1;
}


/* one probe string per section. section allowed if all patterns of s and bases match */
static const char *const tp_probe[] = {"-1", "1", "1-01", "1-01-01", "T00", "T00:00", "T00:00:00", "T00:00:00:1", "T00:00:00:1.00", "F1", "+01:00"};
static const char *const dur_probe[] = {"-P", "P1D", "PT", "PT1H", "PT1M", "PT1S", "PT1N", "PT01f", "P1F", "P+01:00Z"};

static int allows(const bim_simple_t *s, const char *v) {
  for (; s; s = s->base)
    for (int i = 0; s->match && i < s->npatterns; i++)
      if (s->match(s->patterns[i], v) != 1) return 0;
  return 1;
}

static unsigned flags_of(const bim_simple_t *s, unsigned compat, const char *const *probe, int n) {
  unsigned m = 0;
  for (int i = 0; i < n; i++)
    if (!(compat & BIM_COMPAT_M7_FLAGS) || allows(s, probe[i])) m |= 1u << i;
  return m;
}

unsigned m7_timepoint_flags(const bim_simple_t *s, unsigned compat) { return flags_of(s, compat, tp_probe, (int)(sizeof tp_probe / sizeof tp_probe[0])); }

unsigned m7_duration_flags(const bim_simple_t *s, unsigned compat) { return flags_of(s, compat, dur_probe, (int)(sizeof dur_probe / sizeof dur_probe[0])); }

/* uncoded section must be unused */
static int wflag(bitwriter_t *bw, unsigned mask, int f, int v) {
  if (M7_HAS(mask, f)) return bit(bw, v);
  return v ? -1 : 0;
}

/* ISO 15938-1:2002 8.5.4.2.5 */
int m7_timepoint_encode(const char *v, const bim_simple_t *sp, unsigned compat, bitwriter_t *bw) {
  m7_timepoint_t t;
  unsigned m = m7_timepoint_flags(sp, compat);
  int w;
  if (m7_timepoint_parse(v, &t)) return -1;
  if ((t.has_month && (t.month < 1 || t.month > 12)) || (t.has_day && (t.day < 1 || t.day > 31)) || (t.time && t.hour > 23) ||
      (t.has_min && t.min > 59) || (t.has_sec && t.sec > 59) || (t.has_ff && t.ff > 99) || (t.has_n && !t.has_frac) ||
      (t.tz && !tz_ok(t.tzh, t.tzm, -15, 16)))
    return -1;
  w = bim_width(t.frac);
  if (t.has_n && (!t.frac || (w < 64 && t.n >> w))) return -1;
  if (compat & BIM_COMPAT_M7_FLAGS) {
    if (wflag(bw, m, M7T_NEG, t.neg) || wflag(bw, m, M7T_DATE, t.date) || (t.date && (wflag(bw, m, M7T_MONTH, t.has_month) || (t.has_month && wflag(bw, m, M7T_DAY, t.has_day))))) return -1;
  } else if (bit(bw, t.date) || (t.date && (bit(bw, t.neg) || bit(bw, t.has_month) || (t.has_month && bit(bw, t.has_day))))) {
    return -1;
  }
  if (wflag(bw, m, M7T_TIME, t.time) ||
      (t.time && (wflag(bw, m, M7T_MIN, t.has_min) ||
                  (t.has_min && (wflag(bw, m, M7T_SEC, t.has_sec) || (t.has_sec && (wflag(bw, m, M7T_N, t.has_n) || (t.has_n && wflag(bw, m, M7T_FF, t.has_ff)))))))))
    return -1;
  if (wflag(bw, m, M7T_FRAC, t.has_frac) || wflag(bw, m, M7T_TZ, t.tz)) return -1;
  if (t.date && (bitwriter_put_vluimsbf5(bw, t.year) || (t.has_month && bitwriter_put(bw, t.month - !!(compat & BIM_COMPAT_M7_FLAGS), 4)) ||
       (t.has_day && bitwriter_put(bw, t.day - !!(compat & BIM_COMPAT_M7_FLAGS), 5)))) return -1;
  if (t.time && (bitwriter_put(bw, t.hour, 5) || (t.has_min && bitwriter_put(bw, t.min, 6)) || (t.has_sec && bitwriter_put(bw, t.sec, 6)))) return -1;
  if (t.has_frac && bitwriter_put_vluimsbf5(bw, t.frac)) return -1;
  if (t.has_n && (bitwriter_put(bw, t.n, w) || (t.has_ff && bitwriter_put(bw, t.ff, 7)))) return -1;
  /* -15..+16 in 5 bits and -31..+32 in 6 bits: unsigned, offset 15 and 31 */
  return t.tz && (bitwriter_put(bw, (uint64_t)(t.tzh + 15), 5) || bitwriter_put(bw, t.tzm / 15, 2)) ? -1 : 0;
}

/* ISO 15938-1:2002 8.5.4.2.6 */
int m7_duration_encode(const char *v, const bim_simple_t *sp, unsigned compat, bitwriter_t *bw) {
  m7_duration_t d;
  unsigned m = m7_duration_flags(sp, compat);
  int w;
  if (m7_duration_parse(v, &d)) return -1;
  if ((d.has[0] && d.has_d && d.v[0] > 23) || (d.has[1] && d.has[0] && d.v[1] > 59) || (d.has[2] && d.has[1] && d.v[2] > 59) || (d.has_ff && d.ff > 99) ||
      (d.has[3] && d.has[2] && !d.has_frac) || (d.tz && !tz_ok(d.tzh, d.tzm, -31, 32)))
    return -1;
  w = bim_width(d.frac);
  if (d.has[3] && d.has[2] && (!d.frac || (w < 64 && d.v[3] >> w))) return -1;
  if (wflag(bw, m, M7D_NEG, d.neg) || wflag(bw, m, M7D_D, d.has_d) || wflag(bw, m, M7D_T, d.t)) return -1;
  for (int u = 0; d.t && u < 4; u++)
    if (wflag(bw, m, M7D_H + u, d.has[u])) return -1;
  if ((d.t && wflag(bw, m, M7D_FF, d.has_ff)) || wflag(bw, m, M7D_FRAC, d.has_frac) || wflag(bw, m, M7D_TZ, d.tz)) return -1;
  if (d.has_d && bitwriter_put_vluimsbf5(bw, d.d)) return -1;
  for (int u = 0; d.t && u < 3; u++) {
    int fixed = u ? d.has[u - 1] : d.has_d;
    int width = u ? 6 : 5;
    if (d.has[u] && (fixed ? bitwriter_put(bw, d.v[u], width) : bitwriter_put_vluimsbf5(bw, d.v[u]))) return -1;
  }
  if (d.has_frac && bitwriter_put_vluimsbf5(bw, d.frac)) return -1;
  if (d.has[3] && (d.has[2] ? bitwriter_put(bw, d.v[3], w) : bitwriter_put_vluimsbf5(bw, d.v[3]))) return -1;
  if (d.has_ff && bitwriter_put(bw, d.ff, 7)) return -1;
  return d.tz && (bitwriter_put(bw, (uint64_t)(d.tzh + 31), 6) || bitwriter_put(bw, d.tzm / 15, 2)) ? -1 : 0;
}
