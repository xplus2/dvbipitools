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
  if (mjd < 0 || mjd > 0xFFFFFFFFL) return -1;
  return bitwriter_put(bw, (uint64_t)mjd, 32) || bitwriter_put(bw, (uint64_t)(sec * 1000L + ms), 32) ? -1 : 0;
}

int tva_datetime_decode(bitreader_t *br, char *out, size_t cap) {
  uint64_t mjd;
  uint64_t ms;
  int y;
  int mo;
  int d;
  int n;
  if (bitreader_get(br, 32, &mjd) || bitreader_get(br, 32, &ms) || ms >= (uint64_t)MS_PER_DAY || mjd > 0xFFFFFu) return -1;
  mjd_to_date((long)mjd, &y, &mo, &d);
  n = snprintf(out, cap, "%04d-%02d-%02dT%02u:%02u:%02u", y, mo, d, (unsigned)(ms / 3600000), (unsigned)(ms / 60000 % 60), (unsigned)(ms / 1000 % 60));
  if (n < 0 || (size_t)n >= cap) return -1;
  if (ms % 1000) {
    char frac[8];
    size_t k;
    snprintf(frac, sizeof frac, ".%03u", (unsigned)(ms % 1000));
    k = strlen(frac);
    while (frac[k - 1] == '0') k--;
    frac[k] = '\0';
    if ((size_t)n + strlen(frac) >= cap) return -1;
    strcpy(out + n, frac);
    n += (int)strlen(frac);
  }
  if ((size_t)n + 2 > cap) return -1;
  strcpy(out + n, "Z");
  return 0;
}

int tva_date_encode(bitwriter_t *bw, const char *v) {
  int y;
  int mo;
  int d;
  int used = 0;
  long mjd;
  char back[16];
  if (sscanf(v, "%4d-%2d-%2d%n", &y, &mo, &d, &used) != 3 || v[used] || mo < 1 || mo > 12 || d < 1 || d > 31) return -1;
  mjd = date_to_mjd(y, mo, d);
  mjd_to_date(mjd, &y, &mo, &d);
  snprintf(back, sizeof back, "%04d-%02d-%02d", y, mo, d);
  if (strcmp(back, v) || mjd < 0 || mjd > 0xFFFFFFFFL) return -1;
  return bitwriter_put(bw, (uint64_t)mjd, 32) ? -1 : 0;
}

int tva_date_decode(bitreader_t *br, char *out, size_t cap) {
  uint64_t mjd;
  int y;
  int mo;
  int d;
  int n;
  if (bitreader_get(br, 32, &mjd) || mjd > 0xFFFFFu) return -1;
  mjd_to_date((long)mjd, &y, &mo, &d);
  n = snprintf(out, cap, "%04d-%02d-%02d", y, mo, d);
  return n < 0 || (size_t)n >= cap ? -1 : 0;
}
