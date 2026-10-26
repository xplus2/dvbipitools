/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_AOD_H
#define DVBIPITOOLS_LIB_BIM_AOD_H

#include "bimio.h"
#include "codec.h"
#include "decoderinit.h"

/* ISO 23001-1 8.2 .. 8.8 (Amd 2) */

/* cfg: instances + mappings of DecoderInit or FUU (8.3), types: AdvancedOptimisedDecoderTypeURI list of di.
   type codes per 6.6.5.4 with anyType base. *out freed by bim_aod_free */
int bim_aod_build(const bim_decoderinit_t *di, const bim_decoderinit_t *cfg, const bim_schema_t *schemas, int nschemas, bim_aod_t **out, bim_err_t *err);
void bim_aod_free(bim_aod_t *a);
/* 8.5.3: vmin + vq (vmax - vmin) / (2^nbits - 1) */
double bim_aod_dequant(const bim_aod_inst_t *in, uint64_t vq, int nbits);

#define BIM_AOD_ERR "LE400307"

#endif
