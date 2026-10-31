/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_DES_H
#define DVBIPITOOLS_LIB_CAS_DES_H

#include <stdint.h>

#define DES_BLOCK_LEN 8
#define DES_KEY56_LEN 7

typedef struct {
  uint32_t k[32];
} des_key_t;

/* 7 key bits per byte, odd parity in lsb */
void des_key_expand(const unsigned char in[DES_KEY56_LEN], unsigned char out[DES_BLOCK_LEN]);

void des_set_key(des_key_t *ks, const unsigned char key[DES_BLOCK_LEN]);
void des_encrypt(const des_key_t *ks, const unsigned char in[DES_BLOCK_LEN], unsigned char out[DES_BLOCK_LEN]);
void des_decrypt(const des_key_t *ks, const unsigned char in[DES_BLOCK_LEN], unsigned char out[DES_BLOCK_LEN]);

/* EDE decrypt: D(k3), E(k2), D(k1). in/out may alias */
void des3_decrypt(const des_key_t *k1, const des_key_t *k2, const des_key_t *k3, const unsigned char in[DES_BLOCK_LEN], unsigned char out[DES_BLOCK_LEN]);

#endif
