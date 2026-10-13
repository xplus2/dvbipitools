/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_PMT_FILTER_H
#define DVBIPITOOLS_LIB_MUX_PMT_FILTER_H

#include <stddef.h>

/* PMT section minus drop_pids. program_info descr unchanged. append CRC32, patch section_length. 0 on broken input or out_cap overflow. */
size_t pmt_filter_rewrite(const unsigned char *pmt_section, size_t seclen, const unsigned *drop_pids, size_t n_drop, unsigned char *out, size_t out_cap);

/* packets needed for the largest section (4096 bytes) incl. pointer_field */
#define PMT_FILTER_PKT_MAX 23

/* one PSI section -> one 188-byte TS packet (PUSI, pointer_field 0), 0xFF padded. 0 if section doesn't fit */
int pmt_filter_emit_packet(unsigned char *out188, unsigned pid, unsigned char cc, const unsigned char *sec, size_t seclen);

/* PSI section -> n consecutive TS packets (PUSI + pointer_field 0 on the first), 0xFF padded.
   *cc: last used on entry, bumped per packet. returns n, 0 if out_cap (bytes) is too small */
size_t pmt_filter_emit_packets(unsigned char *out, size_t out_cap, unsigned pid, unsigned char *cc, const unsigned char *sec, size_t seclen);

#endif
