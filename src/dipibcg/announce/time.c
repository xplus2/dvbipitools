/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <time.h>

#include "lib/helper/dateutil.h"
#include "dipibcg/announce.h"
#include "dipibcg/announce/announce_int.h"

int iso8601_to_minutes(const char *in, long *out) {
  iso8601_t f;
  if (iso8601_split(in, &f)) return -1;
  *out = iso8601_mjd_minutes(&f);
  return 0;
}

long now_minutes(void) {
  time_t t = time(NULL);
  struct tm tm;
  gmtime_r(&t, &tm);
  return date_to_mjd(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday) * 1440L + tm.tm_hour * 60L + tm.tm_min;
}

long minutes_to_unix(long mjd_minutes) {
  return (mjd_minutes - MJD_UNIX_EPOCH * 1440L) * 60L;
}
