/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "strrepo.h"

#define STRREPO_ENCODING_UTF8 0x01

static int grow(strrepo_writer_t *sw, size_t more) {
  if (sw->len + more <= sw->cap)
    return 0;
  {
    size_t newcap = sw->cap ? sw->cap * 2 : 256;
    while (newcap < sw->len + more)
      newcap *= 2;
    {
      unsigned char *np = realloc(sw->buf, newcap);
      if (!np)
        return -1;
      sw->buf = np;
      sw->cap = newcap;
    }
  }
  return 0;
}

void strrepo_writer_init(strrepo_writer_t *sw) {
  sw->buf = NULL;
  sw->cap = 0;
  sw->len = 0;
  if (!grow(sw, 1))
    sw->buf[sw->len++] = STRREPO_ENCODING_UTF8;
}

void strrepo_writer_free(strrepo_writer_t *sw) {
  free(sw->buf);
  sw->buf = NULL;
  sw->cap = 0;
  sw->len = 0;
}

int strrepo_writer_put(strrepo_writer_t *sw, const char *s) {
  size_t n = strlen(s);
  if (grow(sw, n + 1))
    return -1;
  memcpy(sw->buf + sw->len, s, n);
  sw->len += n;
  sw->buf[sw->len++] = '\0';
  return 0;
}

const unsigned char *strrepo_writer_data(const strrepo_writer_t *sw, size_t *out_len) {
  *out_len = sw->len;
  return sw->buf;
}

int strrepo_reader_init(strrepo_reader_t *sr, const unsigned char *buf, size_t len) {
  sr->buf = buf;
  sr->len = len;
  sr->pos = 0;
  if (len < 1 || buf[0] != STRREPO_ENCODING_UTF8)
    return -1;
  sr->pos = 1;
  return 0;
}

int strrepo_reader_next(strrepo_reader_t *sr, char *out, size_t outcap) {
  size_t start = sr->pos, i, oi = 0;
  if (start >= sr->len)
    return -1;
  for (i = start; i < sr->len && sr->buf[i] != '\0'; i++)
    ;
  if (i >= sr->len)
    return -1;
  for (i = start; i < sr->len && sr->buf[i] != '\0' && oi + 1 < outcap; i++)
    out[oi++] = (char)sr->buf[i];
  out[oi] = '\0';
  sr->pos = i + 1;
  return 0;
}
