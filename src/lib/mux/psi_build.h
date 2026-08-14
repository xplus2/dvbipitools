/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_PSI_BUILD_H
#define DVBIPITOOLS_LIB_MUX_PSI_BUILD_H

#include <stddef.h>

/* section framing helpers, shared with tool-private PMT/EIT builders */
void psi_put16(unsigned char *p, unsigned v);

/* largest prefix of s (len bytes, valid UTF-8) that fits in max_bytes without
   splitting a multi-byte sequence */
size_t psi_utf8_clamp(const char *s, size_t len, size_t max_bytes);

/* DVB text: 0x15 (UTF-8) prefix + bytes, truncated to fit cap without splitting a codepoint */
size_t psi_put_text(unsigned char *out, size_t cap, const char *s);
/* appends CRC32, patches section_length. flags_nibble: 0xB0 (PAT/PMT) or 0xF0 (others) */
size_t psi_finish_section(unsigned char *out, size_t len, size_t cap, unsigned char flags_nibble);

/* each builds one section (table_id..CRC32 incl.). single-program-out shape, content-identical
   whether caller is dipiradiohead or dipitvhead. 0 on overflow */
size_t psi_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned pmt_pid, unsigned char *out, size_t cap);

typedef struct {
  unsigned program_number;
  unsigned pmt_pid;
} psi_pat_entry_t;

/* MPTS PAT: one program_number/pmt_pid pair per entry, all in a single section (fine up to
   ~250 programs before the 1021-byte section cap would matter - well beyond any real use here).
   0 on overflow. */
size_t psi_build_pat_multi(unsigned tsid, unsigned version, const psi_pat_entry_t *programs, size_t n_programs, unsigned char *out, size_t cap);
/* service_type: DVB SI table, e.g. 0x01 digital television, 0x02 digital radio sound */
size_t psi_build_sdt(unsigned version, unsigned tsid, unsigned onid, unsigned service_id, unsigned service_type, const char *provider, const char *service, unsigned char *out, size_t cap);

typedef struct {
  unsigned service_id;
  unsigned service_type;
  const char *provider;
  const char *service_name;
} psi_sdt_entry_t;

/* MPTS SDT: one service_descriptor per entry, one section (same entry-count ceiling as psi_build_pat_multi).
   do not call psi_build_sdt() per service into the same tsid/onid instead. same table_id/tsid/onid/version = one sub_table (EN 300 468 5.2.3/3.1), and per-service
   "0/0" section_number/last_section_number collide, non-first ones dropped by parsers. 0 on overflow or n_services 0. */
size_t psi_build_sdt_multi(unsigned version, unsigned tsid, unsigned onid, const psi_sdt_entry_t *services, size_t n_services, unsigned char *out, size_t cap);
size_t psi_build_nit(unsigned version, unsigned onid, unsigned tsid, const char *network_name, unsigned char *out, size_t cap);
/* table_id 0x01, program-level descriptor loop only (CA_descriptor etc). desc/desc_len: NULL/0 if none */
size_t psi_build_cat(unsigned version, const unsigned char *desc, size_t desc_len, unsigned char *out, size_t cap);

#endif
