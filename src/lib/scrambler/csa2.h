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

/* classic DVB-CSA (libdvbcsa). encrypts len bytes in place, any length */
void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len);

/* inverse of csa2_encrypt_block: decrypts len bytes in place, any length */
void csa2_decrypt_block(csa2_key_t *k, unsigned char *data, size_t len);

/* max entries per csa2_encrypt_batch/csa2_decrypt_batch call. 0 if this
   build has no libdvbcsa (csa2_key_new already fails first in that case,
   so the batch functions are unreachable). */
unsigned csa2_batch_size(void);

typedef struct {
  unsigned char *data;
  size_t len;
} csa2_batch_entry_t;

/* SIMD bitslice CSA: encrypts n entries in place, all under k's key. all entries share one key/parity. n must not exceed csa2_batch_size() */
void csa2_encrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n);

/* inverse of csa2_encrypt_batch */
void csa2_decrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n);

#endif
