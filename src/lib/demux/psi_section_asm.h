/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_PSI_SECTION_ASM_H
#define DVBIPITOOLS_LIB_DEMUX_PSI_SECTION_ASM_H

#include <stddef.h>

#define PSI_SECTION_ASM_BUF_LEN 4096 /* TS private section length cap */

typedef struct {
  int active;
  size_t len;
  size_t expect; /* total section length, 0 until known */
  unsigned char buf[PSI_SECTION_ASM_BUF_LEN];
} psi_section_asm_t;

/* accumulates one section from a pid's TS-packet payloads. pl/plen: payload past  adaptation field (caller strips it).
   pusi: packet had payload_unit_start_indicator set (payload starts with a pointer_field).
   1 when a->buf[0..len) holds one complete section, 0 while still accumulating or resynced past an oversized section. */
int psi_section_asm_feed(psi_section_asm_t *a, const unsigned char *pl, size_t plen, int pusi);

#endif
