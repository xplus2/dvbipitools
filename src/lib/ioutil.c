/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ioutil.h"

int read_all(FILE *f, char **out, size_t *out_len) {
  size_t cap = 65536, len = 0;
  char *buf = malloc(cap);
  if (!buf)
    return -1;
  for (;;) {
    size_t n;
    if (len + 4096 + 1 > cap) {
      char *p;
      cap *= 2;
      p = realloc(buf, cap);
      if (!p) {
        free(buf);
        return -1;
      }
      buf = p;
    }
    n = fread(buf + len, 1, 4096, f);
    len += n;
    if (n < 4096) {
      if (ferror(f)) {
        free(buf);
        return -1;
      }
      break;
    }
  }
  buf[len] = '\0';
  *out = buf;
  *out_len = len;
  return 0;
}

size_t bufcpy(char *dst, size_t dstsz, const char *src) {
  size_t len = strlen(src);
  if (dstsz) {
    size_t n = len < dstsz - 1 ? len : dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
  }
  return len;
}

char *find_header_end(char *b, size_t n, size_t *termlen) {
  size_t i;
  for (i = 0; i + 1 < n; i++) {
    if (b[i] == '\n' && b[i + 1] == '\n') {
      *termlen = 2;
      return b + i;
    }
    if (i + 3 < n && b[i] == '\r' && b[i + 1] == '\n' && b[i + 2] == '\r' && b[i + 3] == '\n') {
      *termlen = 4;
      return b + i;
    }
  }
  return NULL;
}

void *array_grow(void *arr, int *cap, int need, size_t elemsz) {
  int newcap;
  void *p;
  if (need <= *cap)
    return arr;
  newcap = *cap ? *cap * 2 : 16;
  if (newcap < need)
    newcap = need;
  p = realloc(arr, (size_t)newcap * elemsz);
  if (!p)
    return NULL;
  *cap = newcap;
  return p;
}

int all_digits(const char *s, int n) {
  int i;
  for (i = 0; i < n; i++)
    if (!isdigit((unsigned char)s[i]))
      return 0;
  return 1;
}

int iso8601_split(const char *in, iso8601_t *out) {
  size_t l = strlen(in);
  const char *tail;

  if (l < 19 || in[4] != '-' || in[7] != '-' || in[10] != 'T' || in[13] != ':' || in[16] != ':')
    return -1;
  if (!all_digits(in, 4) || !all_digits(in + 5, 2) || !all_digits(in + 8, 2) ||
      !all_digits(in + 11, 2) || !all_digits(in + 14, 2) || !all_digits(in + 17, 2))
    return -1;
  out->y = (in[0] - '0') * 1000 + (in[1] - '0') * 100 + (in[2] - '0') * 10 + (in[3] - '0');
  out->mo = (in[5] - '0') * 10 + (in[6] - '0');
  out->d = (in[8] - '0') * 10 + (in[9] - '0');
  out->h = (in[11] - '0') * 10 + (in[12] - '0');
  out->mi = (in[14] - '0') * 10 + (in[15] - '0');
  out->s = (in[17] - '0') * 10 + (in[18] - '0');
  out->off_min = 0;
  tail = in + 19;
  if (*tail == 'Z') {
    out->offset_kind = ISO8601_OFF_Z;
    return 0;
  }
  if (*tail == '\0') {
    out->offset_kind = ISO8601_OFF_NONE;
    return 0;
  }
  if ((*tail == '+' || *tail == '-') && strlen(tail) >= 6 && tail[3] == ':' &&
      all_digits(tail + 1, 2) && all_digits(tail + 4, 2)) {
    int oh = (tail[1] - '0') * 10 + (tail[2] - '0');
    int om = (tail[4] - '0') * 10 + (tail[5] - '0');
    out->off_min = (oh * 60 + om) * (tail[0] == '-' ? -1 : 1);
    out->offset_kind = ISO8601_OFF_NUMERIC;
    return 0;
  }
  return -1;
}

long date_to_mjd(int y, int mo, int d) {
  int yy = mo <= 2 ? y - 1 : y;
  int mm = mo <= 2 ? mo + 12 : mo;
  return 14956L + d + (long)((yy - 1900) * 365.25) + (long)((mm + 1) * 30.6001);
}
