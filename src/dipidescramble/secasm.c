/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "secasm.h"

int secasm_feed(secasm_t *a, const unsigned char *pl, size_t plen, int pusi) {
  size_t i = 0;

  if (pusi) {
    unsigned ptr;
    if (plen < 1)
      return 0;
    ptr = pl[0];
    i = 1 + (size_t)ptr;
    if (i > plen) {
      a->active = 0;
      return 0;
    }
    a->len = 0;
    a->expect = 0;
    a->active = 1;
  } else if (!a->active) {
    return 0;
  }
  for (; i < plen; i++) {
    if (a->len < sizeof a->buf)
      a->buf[a->len++] = pl[i];
    if (a->expect == 0 && a->len >= 3) {
      unsigned sl = (((unsigned)a->buf[1] & 0x0F) << 8) | a->buf[2];
      a->expect = (size_t)sl + 3;
      if (a->expect > sizeof a->buf) {
        a->active = 0;
        return 0;
      }
    }
    if (a->expect != 0 && a->len >= a->expect) {
      a->active = 0;
      return 1;
    }
  }
  return 0;
}

const unsigned char *secasm_section(const secasm_t *a, size_t *len) {
  if (len)
    *len = a->expect;
  return a->buf;
}
