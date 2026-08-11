/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_PMTBUILD_H
#define DIPITVHEAD_PMTBUILD_H

#include <stddef.h>

#include "lib/demux/psi/psi.h"

#define OUT_PID_PAT 0x0000
#define OUT_PID_CAT 0x0001
#define OUT_PID_NIT 0x0010
#define OUT_PID_SDT 0x0011
#define OUT_PID_EIT 0x0012 /* fixed DVB-SI pid (ETSI EN 300 468), never per-program */
#define OUT_PID_PMT_BASE 0x1000 /* + prog_idx, matches ffmpeg's pmt_start_pid default */
#define OUT_PROGRAM_BLOCK_BASE 0x0100
#define OUT_MAX_ES 32 /* per-program block: video+ES(cap) plus one AIT slot */
#define OUT_PROGRAM_ES_CAP (OUT_MAX_ES - 1) /* last block slot is AIT */

typedef struct {
  unsigned pmt_pid, video_pid, es_pid_base, ait_pid;
} out_program_pids_t;

/* fixed pid block from 0-based program index (always 0 for standalone). idx=0 keeps
   pmt/video/es_pid_base unchanged (0x1000/0x0100/0x0101); ait_pid moves off the old 0x0020
   default (collided with default --cas-ecm-pid) into the per-program block. */
void out_program_pids(unsigned idx, out_program_pids_t *out);

typedef struct {
  unsigned in_pid;  /* source pid, for remux's packet -> out_es_t lookup */
  unsigned out_pid;
  unsigned stream_type; /* our own output stream_type, not the source's */
  const psi_es_t *src;  /* borrowed, valid as long as the discovery psi_t is alive */
} out_es_t;

/* video -> video_pid, rest -> es_pid_base.. in order. drops unsupported ES.
   retval: count, *pcr_pid = mapped output pid of the PCR ES (or first ES fallback),
   *dropped = supported ES beyond cap, left unmapped, in discovery order */
int pmtbuild_map_es(const psi_es_t *in_es, int in_count, unsigned src_pcr_pid, unsigned video_pid, unsigned es_pid_base, out_es_t *out_es, int cap, unsigned *pcr_pid, int *dropped);

/* build multi-ES PMT section. prog_desc/prog_desc_len: program_info descriptor bytes (e.g. cadescbuild_ca_descriptor()), NULL/0 if none.
   extra/extra_len: pre-built ES-loop bytes appended before the CRC (e.g. aitbuild_pmt_entry()), NULL/0 if none. 0 on overflow */
size_t pmtbuild_pmt(unsigned version, unsigned program_number, unsigned pcr_pid, const unsigned char *prog_desc, size_t prog_desc_len, const out_es_t *es, int es_count, const unsigned char *extra, size_t extra_len, unsigned char *out, size_t cap);

#endif
