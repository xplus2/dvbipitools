/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SCRAMBLER_CISSA_H
#define DVBIPITOOLS_LIB_SCRAMBLER_CISSA_H

#include <stddef.h>

/* only 16 for now, legacy = backlog */
#define CISSA_CW_LEN 16

/* AES-128-CBC per ETSI TS 103 127 clause 6.3: fixed IV, CBC state resets every call
   (each TS packet scrambled independently, for random access).
   len must be a nonzero multiple of 16. 0 on success, -1 on error. */
int cissa_encrypt_block(const unsigned char key[CISSA_CW_LEN], unsigned char *data, size_t len);

#endif
