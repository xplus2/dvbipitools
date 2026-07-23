/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_PMTBUILD_H
#define DIPITVHEAD_PMTBUILD_H

#include <stddef.h>

#include "lib/demux/psi.h"

#define OUT_PID_PAT 0x0000
#define OUT_PID_NIT 0x0010
#define OUT_PID_SDT 0x0011
#define OUT_PID_EIT 0x0012
#define OUT_PID_PMT 0x1000
#define OUT_PID_VIDEO 0x0100
#define OUT_PID_ES_BASE 0x0101 /* audio/subtitle/teletext, assigned sequentially from here */
#define OUT_MAX_ES 32

typedef struct {
  unsigned in_pid;  /* source pid, for remux's packet -> out_es_t lookup */
  unsigned out_pid;
  unsigned stream_type; /* our own output stream_type, not the source's */
  const psi_es_t *src;  /* borrowed, valid as long as the discovery psi_t is alive */
} out_es_t;

/* video -> OUT_PID_VIDEO, rest -> OUT_PID_ES_BASE.. in order. drops unsupported ES.
 * returns count, *pcr_pid = mapped output pid of the PCR ES (or first ES as fallback) */
int pmtbuild_map_es(const psi_es_t *in_es, int in_count, unsigned src_pcr_pid, out_es_t *out_es, int cap, unsigned *pcr_pid);

/* builds our multi-ES PMT section. extra/extra_len: pre-built ES-loop bytes
 * appended before the CRC (e.g. aitbuild_pmt_entry()), NULL/0 if none. 0 on overflow */
size_t pmtbuild_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, const out_es_t *es, int es_count, const unsigned char *extra, size_t extra_len, unsigned char *out, size_t cap);

#endif
