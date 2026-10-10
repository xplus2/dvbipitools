/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <string.h>

#include "../sys/ioutil.h"
#include "timefmt.h"

static const struct {
  const char *name;
  int off_min;
} zone_names[] = {
    {"UT", 0},   {"UTC", 0},  {"GMT", 0},  {"Z", 0},    {"WET", 0},   {"WEST", 60},
    {"BST", 60}, {"CET", 60}, {"CEST", 120}, {"EET", 120}, {"EEST", 180},
};

static int zone_from_name(const char *s, int *off_min) {
  for (size_t i = 0; i < sizeof zone_names / sizeof zone_names[0]; i++) {
    if (!strcmp(s, zone_names[i].name)) {
      *off_min = zone_names[i].off_min;
      return 0;
    }
  }
  return -1;
}

/* [+-]HHMM, 0 ok */
static int zone_from_numeric(const char *s, int *off_min) {
  if ((s[0] != '+' && s[0] != '-') || !all_digits(s + 1, 4) || s[5]) return -1;
  *off_min = ((s[1] - '0') * 10 + (s[2] - '0')) * 60 + (s[3] - '0') * 10 + (s[4] - '0');
  if (s[0] == '-') *off_min = -*off_min;
  return 0;
}

static size_t put_zone(char *buf, int off_min) {
  size_t n = 0;
  int a = off_min < 0 ? -off_min : off_min;
  if (off_min == 0) {
    buf[0] = 'Z';
    return 1;
  }
  buf[n++] = off_min < 0 ? '-' : '+';
  n += uint_to_str_pad(buf + n, (unsigned)(a / 60), 2);
  buf[n++] = ':';
  n += uint_to_str_pad(buf + n, (unsigned)(a % 60), 2);
  return n;
}

int xmltv_time_to_iso8601(const char *in, char *out, size_t outcap) {
  static const char defaults[] = "00000101000000";
  char buf[32]; /* longest case: "YYYY-MM-DDTHH:MM:SS+HH:MM" + NUL, 26 bytes */
  char dig[15];
  size_t nd = 0;
  size_t n = 0;
  const char *z;
  int off_min = 0;
  int has_zone = 1;
  int unknown_zone = 0;

  while (isdigit((unsigned char)in[nd])) nd++;
  if (nd < 4 || nd > 14 || (nd & 1)) return -1;
  memcpy(dig, defaults, 14);
  memcpy(dig, in, nd);
  dig[14] = '\0';

  z = in + nd;
  while (*z == ' ' || *z == '\t') z++;
  if (*z) {
    if (zone_from_numeric(z, &off_min) != 0 && zone_from_name(z, &off_min) != 0) {
      if (!isalpha((unsigned char)z[0])) return -1;
      unknown_zone = 1;
      has_zone = 0;
    }
  }

  memcpy(buf, dig, 4);
  n = 4;
  buf[n++] = '-';
  memcpy(buf + n, dig + 4, 2);
  n += 2;
  buf[n++] = '-';
  memcpy(buf + n, dig + 6, 2);
  n += 2;
  buf[n++] = 'T';
  memcpy(buf + n, dig + 8, 2);
  n += 2;
  buf[n++] = ':';
  memcpy(buf + n, dig + 10, 2);
  n += 2;
  buf[n++] = ':';
  memcpy(buf + n, dig + 12, 2);
  n += 2;
  if (has_zone) n += put_zone(buf + n, off_min);
  buf[n] = '\0';
  bufcpy(out, outcap, buf);
  return unknown_zone ? 1 : 0;
}

int iso8601_to_xmltv_time(const char *in, char *out, size_t outcap) {
  iso8601_t f;
  char buf[24]; /* longest case: "YYYYMMDDHHMMSS +HHMM" + NUL, 21 bytes */
  size_t n;

  if (iso8601_split(in, &f)) return -1;
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
