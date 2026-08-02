/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SCRAMBLER_CSA2_H
#define DVBIPITOOLS_LIB_SCRAMBLER_CSA2_H

#include <stddef.h>

#define CSA2_CW_LEN 8

typedef struct csa2_key csa2_key_t;

/* NULL on error (or if this build has no libdvbcsa) */
csa2_key_t *csa2_key_new(const unsigned char cw[CSA2_CW_LEN]);
void csa2_key_free(csa2_key_t *k);

/* classic DVB-CSA (libdvbcsa). encrypts len bytes (a multiple of 8) in place */
void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len);

#endif
