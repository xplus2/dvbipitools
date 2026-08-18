/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "../ioutil.h"
#include "timefmt.h"

int xmltv_time_to_iso8601(const char *in, char *out, size_t outcap) {
  char y[5], mo[3], d[3], h[3], mi[3], s[3];
  const char *off = NULL;

  if (strlen(in) < 14 || !all_digits(in, 14))
    return -1;
  memcpy(y, in, 4); y[4] = '\0';
  memcpy(mo, in + 4, 2); mo[2] = '\0';
  memcpy(d, in + 6, 2); d[2] = '\0';
  memcpy(h, in + 8, 2); h[2] = '\0';
  memcpy(mi, in + 10, 2); mi[2] = '\0';
  memcpy(s, in + 12, 2); s[2] = '\0';

  if (strlen(in) >= 20 && in[14] == ' ' && (in[15] == '+' || in[15] == '-') && all_digits(in + 16, 4))
    off = in + 15;

  if (off) {
    char sign = off[0];
    char oh[3], om[3];
    memcpy(oh, off + 1, 2); oh[2] = '\0';
    memcpy(om, off + 3, 2); om[2] = '\0';
    if (sign == '+' && !strcmp(oh, "00") && !strcmp(om, "00"))
      snprintf(out, outcap, "%s-%s-%sT%s:%s:%sZ", y, mo, d, h, mi, s);
    else
      snprintf(out, outcap, "%s-%s-%sT%s:%s:%s%c%s:%s", y, mo, d, h, mi, s, sign, oh, om);
  } else {
    snprintf(out, outcap, "%s-%s-%sT%s:%s:%s", y, mo, d, h, mi, s);
  }
  return 0;
}

int iso8601_to_xmltv_time(const char *in, char *out, size_t outcap) {
  iso8601_t f;

  if (iso8601_split(in, &f))
    return -1;

  if (f.offset_kind == ISO8601_OFF_Z) {
    snprintf(out, outcap, "%04d%02d%02d%02d%02d%02d +0000", f.y, f.mo, f.d, f.h, f.mi, f.s);
  } else if (f.offset_kind == ISO8601_OFF_NUMERIC) {
    char sign = f.off_min < 0 ? '-' : '+';
    int abs_min = f.off_min < 0 ? -f.off_min : f.off_min;
    snprintf(out, outcap, "%04d%02d%02d%02d%02d%02d %c%02d%02d", f.y, f.mo, f.d, f.h, f.mi, f.s, sign, abs_min / 60, abs_min % 60);
  } else {
    snprintf(out, outcap, "%04d%02d%02d%02d%02d%02d", f.y, f.mo, f.d, f.h, f.mi, f.s);
  }
  return 0;
}
