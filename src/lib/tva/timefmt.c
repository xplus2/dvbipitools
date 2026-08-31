/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <string.h>

#include "../helper/ioutil.h"
#include "timefmt.h"

int xmltv_time_to_iso8601(const char *in, char *out, size_t outcap) {
  char buf[32]; /* longest case: "YYYY-MM-DDTHH:MM:SS+HH:MM" + NUL, 26 bytes */
  const char *off = NULL;
  size_t n = 0;

  if (strlen(in) < 14 || !all_digits(in, 14))
    return -1;
  if (strlen(in) >= 20 && in[14] == ' ' && (in[15] == '+' || in[15] == '-') && all_digits(in + 16, 4))
    off = in + 15;

  memcpy(buf, in, 4); n = 4;
  buf[n++] = '-';
  memcpy(buf + n, in + 4, 2); n += 2;
  buf[n++] = '-';
  memcpy(buf + n, in + 6, 2); n += 2;
  buf[n++] = 'T';
  memcpy(buf + n, in + 8, 2); n += 2;
  buf[n++] = ':';
  memcpy(buf + n, in + 10, 2); n += 2;
  buf[n++] = ':';
  memcpy(buf + n, in + 12, 2); n += 2;

  if (off) {
    if (off[0] == '+' && !memcmp(off + 1, "0000", 4)) {
      buf[n++] = 'Z';
    } else {
      buf[n++] = off[0];
      memcpy(buf + n, off + 1, 2); n += 2;
      buf[n++] = ':';
      memcpy(buf + n, off + 3, 2); n += 2;
    }
  }
  buf[n] = '\0';
  bufcpy(out, outcap, buf);
  return 0;
}

int iso8601_to_xmltv_time(const char *in, char *out, size_t outcap) {
  iso8601_t f;
  char buf[24]; /* longest case: "YYYYMMDDHHMMSS +HHMM" + NUL, 21 bytes */
  size_t n;

  if (iso8601_split(in, &f))
    return -1;

  n = uint_to_str_pad(buf, (unsigned)f.y, 4);
  n += uint_to_str_pad(buf + n, (unsigned)f.mo, 2);
  n += uint_to_str_pad(buf + n, (unsigned)f.d, 2);
  n += uint_to_str_pad(buf + n, (unsigned)f.h, 2);
  n += uint_to_str_pad(buf + n, (unsigned)f.mi, 2);
  n += uint_to_str_pad(buf + n, (unsigned)f.s, 2);

  if (f.offset_kind == ISO8601_OFF_Z) {
    memcpy(buf + n, " +0000", 6);
    n += 6;
  } else if (f.offset_kind == ISO8601_OFF_NUMERIC) {
    int abs_min = f.off_min < 0 ? -f.off_min : f.off_min;
    buf[n++] = ' ';
    buf[n++] = f.off_min < 0 ? '-' : '+';
    n += uint_to_str_pad(buf + n, (unsigned)(abs_min / 60), 2);
    n += uint_to_str_pad(buf + n, (unsigned)(abs_min % 60), 2);
  }
  buf[n] = '\0';
  bufcpy(out, outcap, buf);
  return 0;
}
