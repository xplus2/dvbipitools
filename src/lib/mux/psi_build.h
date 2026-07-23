/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_PSI_BUILD_H
#define DVBIPITOOLS_LIB_MUX_PSI_BUILD_H

#include <stddef.h>

/* section framing helpers, shared with tool-private PMT/EIT builders */
void psi_put16(unsigned char *p, unsigned v);
/* DVB text: 0x15 (UTF-8) prefix + bytes, truncated to fit cap */
size_t psi_put_text(unsigned char *out, size_t cap, const char *s);
/* appends CRC32 and patches the section_length field. flags_nibble: 0xB0 (PAT/PMT) or 0xF0 (others) */
size_t psi_finish_section(unsigned char *out, size_t len, size_t cap, unsigned char flags_nibble);

/* each builds one section (table_id..CRC32 inclusive). single-program-out shape, content-identical
 * whether the caller is dipiradiohead or dipitvhead. 0 on overflow */
size_t psi_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned pmt_pid, unsigned char *out, size_t cap);
/* service_type: DVB SI table, e.g. 0x01 digital television, 0x02 digital radio sound */
size_t psi_build_sdt(unsigned version, unsigned tsid, unsigned onid, unsigned service_id, unsigned service_type, const char *provider, const char *service, unsigned char *out, size_t cap);
size_t psi_build_nit(unsigned version, unsigned onid, unsigned tsid, const char *network_name, unsigned char *out, size_t cap);

#endif
