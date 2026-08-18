/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_CAS_DEVICE_CRYPTO_H
#define LIB_CAS_DEVICE_CRYPTO_H

#include <stddef.h>

#include <openssl/evp.h>

#define CRYPTO_KEY_LEN 32 /* AES-256 key: BK, SK */
#define CRYPTO_GCM_NONCE_LEN 12
#define CRYPTO_GCM_TAG_LEN 16
#define CRYPTO_EMM_G_LEN (CRYPTO_GCM_NONCE_LEN + CRYPTO_KEY_LEN + CRYPTO_GCM_TAG_LEN) /* 60 */
#define CRYPTO_CW_ENC_LEN 16 /* one AES-256-ECB block */

int device_key_load(const char *path, EVP_PKEY **out);

/* EMM-U: RSA-OAEP decrypt -> BK */
int device_emm_u_decrypt(EVP_PKEY *ek, const unsigned char *ct, size_t ct_len, unsigned char bk_out[CRYPTO_KEY_LEN]);

/* EMM-G: AES-256-GCM decrypt (in = nonce||ct||tag, CRYPTO_EMM_G_LEN bytes) -> SK */
int device_emm_g_decrypt(const unsigned char bk[CRYPTO_KEY_LEN], const unsigned char in[CRYPTO_EMM_G_LEN], unsigned char sk_out[CRYPTO_KEY_LEN]);

/* ECM: AES-256-ECB decrypt one block -> cw_len-byte CW (leading bytes of block, rest zero-padding) */
int device_ecm_decrypt(const unsigned char sk[CRYPTO_KEY_LEN], const unsigned char enc[CRYPTO_CW_ENC_LEN], int cw_len, unsigned char *cw_out);

#endif
