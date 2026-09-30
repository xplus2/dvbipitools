/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_PMTBUILD_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_PMTBUILD_H

#include <stddef.h>

#include "lib/demux/psi/psi.h"

#define ESBUILD_MAX_ES 8
#define ESBUILD_PMT_PID 0x0100u
#define ESBUILD_FIRST_ES_PID 0x0101u

typedef struct {
  unsigned pid;
  codec_t codec;
} esbuild_es_t;

/* video first if present, else the first entry, becomes PCR_PID. sequential PIDs from
   ESBUILD_FIRST_ES_PID, in codecs[] order. n capped at ESBUILD_MAX_ES. returns count assigned */
unsigned esbuild_assign_pids(const codec_t *codecs, unsigned n, esbuild_es_t *out);

size_t esbuild_build_pat(unsigned tsid, unsigned version, unsigned program_number, unsigned char *out, size_t cap);

size_t esbuild_build_pmt(unsigned version, unsigned program_number, const esbuild_es_t *es, unsigned n_es, unsigned char *out, size_t cap);

#endif
