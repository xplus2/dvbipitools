/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_SCRAMBLER_CISSA_H
#define DVBIPITOOLS_LIB_SCRAMBLER_CISSA_H

#include <stddef.h>

/* only 16 for now, legacy = backlog */
#define CISSA_CW_LEN 16

typedef struct cissa_key cissa_key_t;

/* AES-128 key schedule for one CW, built once, reused per packet. NULL on error / no OpenSSL. */
cissa_key_t *cissa_key_new(const unsigned char cw[CISSA_CW_LEN]);
void cissa_key_free(cissa_key_t *k);

/* AES-128-CBC per ETSI TS 103 127 $6.3: fixed IV, CBC state resets every call (each TS packet scrambled independently, for random access).
   len must be a nonzero multiple of 16. 0 on success, -1 on error. */
int cissa_encrypt_block(cissa_key_t *k, unsigned char *data, size_t len);

/* inverse of cissa_encrypt_block. len must be a nonzero multiple of 16. 0 on success, -1 on error. */
int cissa_decrypt_block(cissa_key_t *k, unsigned char *data, size_t len);

#endif
