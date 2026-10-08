/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_PSI_SECTION_ASM_H
#define DVBIPITOOLS_LIB_DEMUX_PSI_SECTION_ASM_H

#include <stddef.h>

#define PSI_SECTION_ASM_BUF_LEN 4096 /* TS private section length cap */

typedef struct {
  int active;
  int cc_seen;
  unsigned cc;
  size_t len;
  size_t expect; /* total section length, 0 until known */
  size_t next;   /* payload offset past last completed section, 0 if none */
  unsigned char buf[PSI_SECTION_ASM_BUF_LEN];
} psi_section_asm_t;

/* call per payload packet b4 feed with a 188 B TS pkg.
   0: duplicate packet inside a section in progress, skip.
   cc gap drops in progress section unless adaptation field sets discontinuity indicator */
int psi_section_asm_cc(psi_section_asm_t *a, const unsigned char *pkt);

/* accumulates one section from a pid's TS-packet payloads. pl/plen: payload past  adaptation field (caller strips it).
   pusi: packet had payload_unit_start_indicator set (payload starts with a pointer_field).
   1 when a->buf[0..len) holds one complete section, 0 while still accumulating or resynced past an oversized section. */
int psi_section_asm_feed(psi_section_asm_t *a, const unsigned char *pl, size_t plen, int pusi);

/* after feed/next returned 1 with same pl/plen: assemble next section pkg
   1 a->buf has 2nd complete section, 0 = stuffing, partial, empty */
int psi_section_asm_next(psi_section_asm_t *a, const unsigned char *pl, size_t plen);

#endif
