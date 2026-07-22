/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "timefmt.h"

static int all_digits(const char *s, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

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
  char y[5], mo[3], d[3], h[3], mi[3], s[3];
  size_t l = strlen(in);
  const char *tail;

  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  memcpy(y, in, 4); y[4] = '\0';
  memcpy(mo, in + 5, 2); mo[2] = '\0';
  memcpy(d, in + 8, 2); d[2] = '\0';
  memcpy(h, in + 11, 2); h[2] = '\0';
  memcpy(mi, in + 14, 2); mi[2] = '\0';
  memcpy(s, in + 17, 2); s[2] = '\0';

  tail = in + 19;
  if (*tail == 'Z') {
    snprintf(out, outcap, "%s%s%s%s%s%s +0000", y, mo, d, h, mi, s);
  } else if ((*tail == '+' || *tail == '-') && strlen(tail) >= 6 && tail[3] == ':') {
    char sign = tail[0];
    char oh[3] = {tail[1], tail[2], '\0'};
    char om[3] = {tail[4], tail[5], '\0'};
    snprintf(out, outcap, "%s%s%s%s%s%s %c%s%s", y, mo, d, h, mi, s, sign, oh, om);
  } else {
    snprintf(out, outcap, "%s%s%s%s%s%s", y, mo, d, h, mi, s);
  }
  return 0;
}
