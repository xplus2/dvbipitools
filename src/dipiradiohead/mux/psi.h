/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_MUX_PSI_H
#define DIPIRADIOHEAD_MUX_PSI_H

#include <stddef.h>

/* each builds one section (table_id..CRC32 inclusive), matching lib/demux/psi.c's parser layout. 0 on overflow */

/* prog_desc/prog_desc_len: program_info descriptors, before ES loop. NULL/0 = none. */
size_t psi_build_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, unsigned stream_type, unsigned es_pid, const unsigned char *prog_desc, size_t prog_desc_len, unsigned char *out, size_t cap);
/* present event only (no following); duration_s is a nominal placeholder, real remaining time is unknown */
size_t psi_build_eit(unsigned version, unsigned service_id, unsigned tsid, unsigned onid, const char *artist, const char *title, unsigned duration_s, unsigned char *out, size_t cap);

#endif
