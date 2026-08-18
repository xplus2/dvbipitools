/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_CRYPTO_H
#define DIPIDESCRAMBLE_CRYPTO_H

#include <stdint.h>

#include "lib/cas/device_crypto.h"

#define CRYPTO_HMAC_SHA256_LEN 32

typedef enum {
  CRYPTO_ECM_AES128, /* 16B key */
  CRYPTO_ECM_AES256, /* 32B key */
  CRYPTO_ECM_DES_EDE, /* 2-key 3DES, 16B key */
  CRYPTO_ECM_DES_EDE3, /* 3-key 3DES, 24B key */
} crypto_ecm_cipher_t;

/* ecm_profile: generic block decrypt, ECB (iv NULL) or CBC. ct_len must be a multiple of cipher's block size
   (16 AES, 8 3DES). key must be cipher's key length. writes ct_len bytes to out. 0 ok, -1 fail */
int crypto_ecm_block_decrypt(crypto_ecm_cipher_t cipher, int cbc, const unsigned char *key, const unsigned char *iv, const unsigned char *ct, size_t ct_len, unsigned char *out);

/* ecm_profile: AES-128/256-GCM decrypt+verify. aad may be NULL/0. 0 ok, -1 fail (incl tag mismatch) */
int crypto_ecm_gcm_decrypt(int key_bits, const unsigned char *key, const unsigned char nonce[CRYPTO_GCM_NONCE_LEN], const unsigned char *aad, size_t aad_len, const unsigned char *ct, size_t ct_len,
                           const unsigned char tag[CRYPTO_GCM_TAG_LEN], unsigned char *out);

/* ecm_profile: one-shot HMAC-SHA256. 0 ok, -1 fail */
int crypto_hmac_sha256(const unsigned char key[CRYPTO_KEY_LEN], const unsigned char *data, size_t len, unsigned char out[CRYPTO_HMAC_SHA256_LEN]);

/* ecm_profile: RFC 5869 HKDF-SHA256, zero salt, single expand round (32B out == one SHA256 block, no T(2) needed). 0 ok, -1 fail */
int crypto_hkdf_sha256(const unsigned char key[CRYPTO_KEY_LEN], const char *info, unsigned char out[CRYPTO_HMAC_SHA256_LEN]);

/* ecm_profile: CRC-32, ieee (0xEDB88320, zlib/png/gzip/ethernet) or castagnoli (0x82F63B78, CRC-32C) polynomial. both: init 0xFFFFFFFF, reflected in/out, final xor 0xFFFFFFFF */
uint32_t crypto_crc32(int castagnoli, const unsigned char *data, size_t len);

#endif
