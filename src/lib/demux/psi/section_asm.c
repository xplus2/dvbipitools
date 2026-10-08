/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../tspack.h"
#include "section_asm.h"

#include <string.h>

static int consume(psi_section_asm_t *a, const unsigned char *pl, size_t plen, size_t i) {
  while (i < plen) {
    size_t need;
    size_t avail;
    size_t take;
    if (a->expect == 0) {
      if (a->len < sizeof a->buf) a->buf[a->len++] = pl[i];
      i++;
      if (a->len >= 3) {
        a->expect = tspack_length12(a->buf + 1) + 3;
        if (a->expect > sizeof a->buf) {
          a->active = 0;
          return 0;
        }
      }
      continue;
    }
    need = a->expect - a->len;
    avail = plen - i;
    take = need < avail ? need : avail;
    memcpy(a->buf + a->len, pl + i, take);
    a->len += take;
    i += take;
    if (a->len >= a->expect) {
      a->active = 0;
      a->next = i;
      return 1;
    }
  }
  return 0;
}

int psi_section_asm_cc(psi_section_asm_t *a, const unsigned char *pkt) {
  unsigned cc = pkt[3] & 0x0F;
  int disc = (pkt[3] & 0x20) && pkt[4] > 0 && (pkt[5] & 0x80);
  if (a->cc_seen) {
    if (cc == a->cc && a->active) return 0;
    if (cc != ((a->cc + 1) & 0x0F) && !disc) a->active = 0;
  }
  a->cc_seen = 1;
  a->cc = cc;
  return 1;
}

int psi_section_asm_feed(psi_section_asm_t *a, const unsigned char *pl, size_t plen, int pusi) {
  size_t i = 0;

  a->next = 0;
  if (pusi) {
    unsigned ptr;
    if (plen < 1) return 0;
    ptr = pl[0];
    i = 1 + (size_t)ptr;
    if (i > plen) {
      a->active = 0;
      return 0;
    }
    if (a->active && ptr && consume(a, pl, i, 1)) {
      a->next = i;
      return 1;
    }
    a->len = 0;
    a->expect = 0;
    a->active = 1;
  } else if (!a->active) {
    return 0;
  }
  return consume(a, pl, plen, i);
}

int psi_section_asm_next(psi_section_asm_t *a, const unsigned char *pl, size_t plen) {
  size_t i = a->next;

  a->next = 0;
  if (i == 0 || i >= plen || pl[i] == 0xFF) return 0;
  a->len = 0;
  a->expect = 0;
  a->active = 1;
  return consume(a, pl, plen, i);
}
