/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "psi_section_asm.h"
#include "tspack.h"

int psi_section_asm_feed(psi_section_asm_t *a, const unsigned char *pl, size_t plen, int pusi) {
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
      a->expect = tspack_length12(a->buf + 1) + 3;
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
