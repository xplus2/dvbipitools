/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/xml_util.h"
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

static long days_from_civil(long y, int m, int d) {
  long era;
  long yoe;
  long doy;
  long doe;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, int *y, int *m, int *d) {
  long era;
  long doe;
  long yoe;
  long doy;
  long mp;
  long yy;
  z += 719468;
  era = (z >= 0 ? z : z - 146096) / 146097;
  doe = z - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  yy = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *m = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yy + (*m <= 2));
}

/* "YYYYMMDDHHMMSS" in UTC: offset applied so equal instants give equal ids. 0 ok, -1 unparseable */
int iso8601_utc_compact(const char *iso, char *out, size_t outcap) {
  iso8601_t t;
  long mins;
  long days;
  int y;
  int mo;
  int d;
  int rem;
  if (iso8601_split(iso, &t) != 0) return -1;
  mins = days_from_civil(t.y, t.mo, t.d) * 1440L + t.h * 60 + t.mi;
  if (t.offset_kind == ISO8601_OFF_NUMERIC) mins -= t.off_min;
  days = mins >= 0 ? mins / 1440 : -((-mins + 1439) / 1440);
  rem = (int)(mins - days * 1440);
  civil_from_days(days, &y, &mo, &d);
  snprintf(out, outcap, "%04d%02d%02d%02d%02d%02d", y, mo, d, rem / 60, rem % 60, t.s);
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

/* xs:duration "[-]PnDTnHnMn[.f]S" to seconds. years/months/negative/empty rejected. 0 ok, -1 bad */
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

/* start_iso + secs, same offset notation, fraction dropped. 0 ok, -1 bad start */
int iso8601_add_secs(const char *start_iso, long secs, char *out, size_t outcap) {
  iso8601_t t;
  long tot;
  int y;
  int mo;
  int d;
  int n;
  if (iso8601_split(start_iso, &t) != 0) return -1;
  tot = days_from_civil(t.y, t.mo, t.d) * 86400L + t.h * 3600L + t.mi * 60L + t.s + secs;
  civil_from_days(tot / 86400, &y, &mo, &d);
  tot %= 86400;
  n = snprintf(out, outcap, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, (int)(tot / 3600), (int)(tot % 3600 / 60), (int)(tot % 60));
  if (n < 0 || (size_t)n >= outcap) return -1;
  if (t.offset_kind == ISO8601_OFF_Z) {
    snprintf(out + n, outcap - (size_t)n, "Z");
  } else if (t.offset_kind == ISO8601_OFF_NUMERIC) {
    int a = t.off_min < 0 ? -t.off_min : t.off_min;
    snprintf(out + n, outcap - (size_t)n, "%c%02d:%02d", t.off_min < 0 ? '-' : '+', a / 60, a % 60);
  }
  return 0;
}
