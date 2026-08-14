/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_CRYPTO_H
#define DIPIDESCRAMBLE_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#include <openssl/evp.h>

#define CRYPTO_KEY_LEN 32 /* AES-256 key: BK, SK */
#define CRYPTO_GCM_NONCE_LEN 12
#define CRYPTO_GCM_TAG_LEN 16
#define CRYPTO_EMM_G_LEN (CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN + CRYPTO_GCM_TAG_LEN) /* 60 */
#define CRYPTO_CW_ENC_LEN 16 /* one AES-256-ECB block */
#define CRYPTO_HMAC_SHA256_LEN 32

int device_key_load(const char *path, EVP_PKEY **out);

/* EMM-U: RSA-OAEP decrypt -> BK */
int device_emm_u_decrypt(EVP_PKEY *ek, const unsigned char *ct, size_t ct_len, unsigned char bk_out[CRYPTO_KEY_LEN]);

/* EMM-G: AES-256-GCM decrypt (in = nonce||ct||tag, CRYPTO_EMM_G_LEN bytes) -> SK */
int device_emm_g_decrypt(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char in[CRYPTO_EMM_G_LEN], unsigned char sk_out[CRYPTO_KEY_LEN]);

/* ECM: AES-256-ECB decrypt one block -> cw_len-byte CW (leading bytes of block, rest zero-padding) */
int device_ecm_decrypt(const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char enc[CRYPTO_CW_ENC_LEN], int cw_len, unsigned char *cw_out);

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
