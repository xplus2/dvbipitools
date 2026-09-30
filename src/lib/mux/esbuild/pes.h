/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_PES_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_PES_H

#include <stddef.h>
#include <stdint.h>

size_t esbuild_pes_build(unsigned char stream_id, uint64_t pts_90k, int has_dts, uint64_t dts_90k, const unsigned char *es, size_t es_len, unsigned char *out, size_t cap);

#endif
