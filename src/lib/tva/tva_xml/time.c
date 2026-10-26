/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/dateutil.h"
#include "lib/tva/tva_xml.h"
#include "lib/tva/tva_xml/tva_xml_int.h"

/* "YYYY-MM-DDTHH:MM:SS..." -> "YYYYMMDDHHMMSS", truncates rest */
void iso8601_compact_prefix(const char *iso, char *out, size_t outcap) {
  size_t oi = 0;
  for (size_t i = 0; iso[i] && oi + 1 < outcap; i++) {
    if (iso[i] != '-' && iso[i] != ':' && iso[i] != 'T') out[oi++] = iso[i];
  }
  out[oi] = '\0';
}

int iso8601_utc_secs(const char *iso, long long *out) {
  iso8601_t t;
  if (iso8601_split(iso, &t) != 0) return -1;
  *out = (long long)(date_to_mjd(t.y, t.mo, t.d) - MJD_UNIX_EPOCH) * 86400 + t.h * 3600L + t.mi * 60L + t.s;
  if (t.offset_kind == ISO8601_OFF_NUMERIC) *out -= t.off_min * 60LL;
  return 0;
}

/* "YYYYMMDDHHMMSS" in UTC: offset applied so equal instants give equal ids. 0 ok, -1 unparseable */
int iso8601_utc_compact(const char *iso, char *out, size_t outcap) {
  long long secs;
  long long days;
  int y;
  int mo;
  int d;
  long rem;
  if (iso8601_utc_secs(iso, &secs) != 0) return -1;
  days = floor_div((long)secs, 86400);
  rem = (long)(secs - days * 86400);
  mjd_to_date((long)days + MJD_UNIX_EPOCH, &y, &mo, &d);
  snprintf(out, outcap, "%04d%02d%02d%02ld%02ld%02ld", y, mo, d, rem / 3600, rem % 3600 / 60, rem % 60);
  return 0;
}

/* strips surrounding XML whitespace in place */
void trim_ws(char *s) {
  size_t n = strlen(s);
  size_t a = 0;
  while (n && isspace((unsigned char)s[n - 1])) n--;
  while (a < n && isspace((unsigned char)s[a])) a++;
  memmove(s, s + a, n - a);
  s[n - a] = '\0';
}

int duration_to_secs(const char *s, long *out) {
  long total = 0;
  long n;
  int in_time = 0;
  int any = 0;
  if (*s++ != 'P') return -1;
  while (*s) {
    if (*s == 'T' && !in_time) {
      in_time = 1;
      s++;
      continue;
    }
    if (!isdigit((unsigned char)*s)) return -1;
    for (n = 0; isdigit((unsigned char)*s); s++) {
      n = n * 10 + (*s - '0');
      if (n > 100000000L) return -1;
    }
    if (in_time && *s == '.' && isdigit((unsigned char)s[1])) {
      s++;
      while (isdigit((unsigned char)*s)) s++;
    }
    if (*s == 'D' && !in_time) n *= 86400;
    else if (*s == 'H' && in_time) n *= 3600;
    else if (*s == 'M' && in_time) n *= 60;
    else if (*s != 'S' || !in_time) return -1;
    total += n;
    any = 1;
    s++;
  }
  if (!any) return -1;
  *out = total;
  return 0;
}

int iso8601_add_secs(const char *start_iso, long secs, char *out, size_t outcap) {
  iso8601_t t;
  long tot;
  long days;
  int n;
  if (iso8601_split(start_iso, &t) != 0) return -1;
  tot = (date_to_mjd(t.y, t.mo, t.d) - MJD_UNIX_EPOCH) * 86400L + t.h * 3600L + t.mi * 60L + t.s + secs;
  days = floor_div(tot, 86400);
  n = iso8601_format(out, outcap, days, tot - days * 86400);
  if (n < 0) return -1;
  if (t.offset_kind == ISO8601_OFF_Z) {
    snprintf(out + n, outcap - (size_t)n, "Z");
  } else if (t.offset_kind == ISO8601_OFF_NUMERIC) {
    int a = t.off_min < 0 ? -t.off_min : t.off_min;
    snprintf(out + n, outcap - (size_t)n, "%c%02d:%02d", t.off_min < 0 ? '-' : '+', a / 60, a % 60);
  }
  return 0;
}

int iso8601_diff_secs(const char *a, const char *b, long *out) {
  long long x;
  long long y;
  if (iso8601_utc_secs(a, &x) || iso8601_utc_secs(b, &y) || y - x > 100000000LL || x - y > 100000000LL) return -1;
  *out = (long)(y - x);
  return 0;
}

int secs_to_duration(long secs, char *out, size_t outcap) {
  int n;
  if (secs < 0) return -1;
  n = snprintf(out, outcap, "PT%ldH%ldM%ldS", secs / 3600, secs % 3600 / 60, secs % 60);
  return n < 0 || (size_t)n >= outcap ? -1 : 0;
}
