/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "dateutil.h"
#include "../sys/ioutil.h"

static int days_in_month(int y, int mo) {
  static const unsigned char dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (mo == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) return 29;
  return dim[mo - 1];
}

int iso8601_split(const char *in, iso8601_t *out) {
  size_t l = strlen(in);
  const char *tail;
  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) || !all_digits(in + 11, 2) || !all_digits(in + 14, 2) || !all_digits(in + 17, 2))
    return -1;
  out->y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  out->mo = (in[5] - '0') * 10 + (in[6] - '0');
  out->d = (in[8] - '0') * 10 + (in[9] - '0');
  out->h = (in[11] - '0') * 10 + (in[12] - '0');
  out->mi = (in[14] - '0') * 10 + (in[15] - '0');
  out->s = (in[17] - '0') * 10 + (in[18] - '0');
  if (out->mo < 1 || out->mo > 12 || out->d < 1 || out->d > days_in_month(out->y, out->mo)) return -1;
  if (out->h > 23 || out->mi > 59 || out->s > 59) return -1;
  out->off_min = 0;
  tail = in + 19;
  if (*tail == '.') {
    const char *fe = tail + 1;
    while (isdigit((unsigned char)*fe)) fe++;
    if (fe == tail + 1) return -1;
    tail = fe;
  }
  if (tail[0] == 'Z' && tail[1] == '\0') {
    out->offset_kind = ISO8601_OFF_Z;
    return 0;
  }
  if (*tail == '\0') {
    out->offset_kind = ISO8601_OFF_NONE;
    return 0;
  }
  if ((*tail == '+' || *tail == '-') && strlen(tail) == 6 && tail[3] == ':' && all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
    int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
    int om = (tail[4] - '0') * 10 + (tail[5] - '0');
    if (oh > 23 || om > 59) return -1;
    out->off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
    out->offset_kind = ISO8601_OFF_NUMERIC;
    return 0;
  }
  return -1;
}

long date_to_mjd(int y, int mo, int d) {
  long yy = mo <= 2 ? y - 1 : y;
  long era = (yy >= 0 ? yy : yy - 399) / 400;
  long yoe = yy - era * 400;
  long doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468 + MJD_UNIX_EPOCH;
}

void mjd_to_date(long mjd, int *y, int *mo, int *d) {
  long z = mjd - MJD_UNIX_EPOCH + 719468;
  long era = (z >= 0 ? z : z - 146096) / 146097;
  long doe = z - era * 146097;
  long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  long mp = (5 * doy + 2) / 153;
  *d = (int)(doy - (153 * mp + 2) / 5 + 1);
  *mo = (int)(mp < 10 ? mp + 3 : mp - 9);
  *y = (int)(yoe + era * 400 + (*mo <= 2));
}

long iso8601_mjd_minutes(const iso8601_t *t) {
  return date_to_mjd(t->y, t->mo, t->d) * 1440L + t->h * 60L + t->mi - t->off_min;
}

int iso8601_format(char *out, size_t cap, long unix_days, long sec_of_day) {
  int y;
  int mo;
  int d;
  int n;
  mjd_to_date(unix_days + MJD_UNIX_EPOCH, &y, &mo, &d);
  n = snprintf(out, cap, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, (int)(sec_of_day / 3600), (int)(sec_of_day % 3600 / 60), (int)(sec_of_day % 60));
  return n < 0 || (size_t)n >= cap ? -1 : n;
}
