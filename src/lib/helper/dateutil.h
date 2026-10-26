/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HELPER_DATEUTIL_H
#define DVBIPITOOLS_LIB_HELPER_DATEUTIL_H

#include <stddef.h>

typedef enum { ISO8601_OFF_NONE, ISO8601_OFF_Z, ISO8601_OFF_NUMERIC } iso8601_offset_kind_t;

typedef struct {
  int y, mo, d, h, mi, s;
  int off_min; /* signed minutes east of UTC, meaningful only if offset_kind == ISO8601_OFF_NUMERIC */
  iso8601_offset_kind_t offset_kind;
} iso8601_t;

/* "YYYY-MM-DDTHH:MM:SS[.f+][Z|+HH:MM|-HH:MM]", fraction dropped, trailing junk rejected.
   No offset gives ISO8601_OFF_NONE, caller picks zone. 0 ok, -1 malformed or field out of range (no leap second) */
int iso8601_split(const char *in, iso8601_t *out);

/* proleptic Gregorian calendar date to modified Julian Day */
long date_to_mjd(int y, int mo, int d);
void mjd_to_date(long mjd, int *y, int *mo, int *d);
long iso8601_mjd_minutes(const iso8601_t *t);
int iso8601_format(char *out, size_t cap, long unix_days, long sec_of_day);

#define MJD_UNIX_EPOCH 40587L

static inline long floor_div(long a, long b) {
  long q = a / b;
  long r = a % b;
  return (r != 0 && ((r < 0) != (b < 0))) ? q - 1 : q;
}

#endif
