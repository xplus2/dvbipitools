/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/dateutil.h"
#include "lib/bim/codec.h"
#include "lib/bim/codec/codec_int.h"

#define MS_PER_DAY 86400000L

int iso_fraction_ms(const char *s, long *ms) {
  const char *p = strchr(s, 'T');
  int digits = 0;
  long v = 0;
  *ms = 0;
  if (!p || strlen(p) < 9 || p[3] != ':' || p[6] != ':' || p[9] != '.') return 0;
  for (p += 10; isdigit((unsigned char)*p); p++, digits++) {
    if (digits < 3) v = v * 10 + (*p - '0');
    else if (*p != '0') return -1;
  }
  while (digits++ < 3) v *= 10;
  *ms = v;
  return 0;
}

int tva_datetime_encode(bitwriter_t *bw, const char *v) {
  iso8601_t t;
  long ms;
  long days;
  long mjd;
  long sec;
  if (iso8601_split(v, &t) || iso_fraction_ms(v, &ms)) return -1;
  mjd = date_to_mjd(t.y, t.mo, t.d);
  sec = t.h * 3600L + t.mi * 60L + t.s - (t.offset_kind == ISO8601_OFF_NUMERIC ? t.off_min * 60L : 0);
  days = floor_div(sec, 86400L);
  mjd += days;
  sec -= days * 86400L;
  if (mjd < 0 || (uint64_t)mjd > 0xFFFFFFFFu) return -1;
  return bitwriter_put(bw, (uint64_t)mjd, 32) || bitwriter_put(bw, (uint64_t)(sec * 1000L + ms), 32) ? -1 : 0;
}

int tva_datetime_decode(bitreader_t *br, char *out, size_t cap) {
  uint64_t mjd;
  uint64_t ms;
  int y;
  int mo;
  int d;
  sbuf_t s;
  if (bitreader_get(br, 32, &mjd) || bitreader_get(br, 32, &ms) || ms >= (uint64_t)MS_PER_DAY || mjd > 0xFFFFFu) return -1;
  mjd_to_date((long)mjd, &y, &mo, &d);
  sbuf_init(&s, out, cap);
  sbuf_add_upad(&s, "", (unsigned)y, 4);
  sbuf_add_upad(&s, "-", (unsigned)mo, 2);
  sbuf_add_upad(&s, "-", (unsigned)d, 2);
  sbuf_add_upad(&s, "T", (unsigned)(ms / 3600000), 2);
  sbuf_add_upad(&s, ":", (unsigned)(ms / 60000 % 60), 2);
  sbuf_add_upad(&s, ":", (unsigned)(ms / 1000 % 60), 2);
  if (ms % 1000) {
    char frac[8];
    size_t k = uint_to_str_pad(frac, (unsigned)(ms % 1000), 3);
    while (frac[k - 1] == '0') k--;
    frac[k] = '\0';
    sbuf_add(&s, ".");
    sbuf_add(&s, frac);
  }
  sbuf_add(&s, "Z");
  return s.truncated ? -1 : 0;
}

int tva_date_encode(bitwriter_t *bw, const char *v) {
  int y;
  int mo;
  int d;
  int used = 0;
  long mjd;
  char back[16];
  sbuf_t s;
  if (sscanf(v, "%4d-%2d-%2d%n", &y, &mo, &d, &used) != 3 || v[used] || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
  mjd = date_to_mjd(y, mo, d);
  mjd_to_date(mjd, &y, &mo, &d);
  sbuf_init(&s, back, sizeof back);
  sbuf_add_upad(&s, "", (unsigned)y, 4);
  sbuf_add_upad(&s, "-", (unsigned)mo, 2);
  sbuf_add_upad(&s, "-", (unsigned)d, 2);
  if (strcmp(back, v) || mjd < 0 || (uint64_t)mjd > 0xFFFFFFFFu) return -1;
  return bitwriter_put(bw, (uint64_t)mjd, 32) ? -1 : 0;
}

int tva_date_decode(bitreader_t *br, char *out, size_t cap) {
  uint64_t mjd;
  int y;
  int mo;
  int d;
  sbuf_t s;
  if (bitreader_get(br, 32, &mjd) || mjd > 0xFFFFFu) return -1;
  mjd_to_date((long)mjd, &y, &mo, &d);
  sbuf_init(&s, out, cap);
  sbuf_add_upad(&s, "", (unsigned)y, 4);
  sbuf_add_upad(&s, "-", (unsigned)mo, 2);
  sbuf_add_upad(&s, "-", (unsigned)d, 2);
  return s.truncated ? -1 : 0;
}
