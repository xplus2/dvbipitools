/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_SECASM_H
#define DIPIDESCRAMBLE_SECASM_H

#include <stddef.h>

#define SECASM_BUF_LEN 4096 /* TS private section length cap, matches lib/demux/psi.c's sect_asm_t */

typedef struct {
  int active;
  size_t len;
  size_t expect; /* total section length incl. header, 0 until known */
  unsigned char buf[SECASM_BUF_LEN];
} secasm_t;

/* feed 1 TS packet's payload (post-adaptation-field) for a single PID reassembly.
   ECM/EMM sections aren't PSI tables psi.c owns, so this is a tool-local duplicate of asm_feed(). */
int secasm_feed(secasm_t *a, const unsigned char *pl, size_t plen, int pusi);

/* completed section bytes + length. only valid right after secasm_feed() returned 1 */
const unsigned char *secasm_section(const secasm_t *a, size_t *len);

#endif
