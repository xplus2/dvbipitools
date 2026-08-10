/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_BISS_CA_H
#define DVBIPITOOLS_LIB_CAS_BISS_CA_H

#include <stddef.h>

#define BISS_CA_EKID_LEN 8           /* entitlement_key_id, 64 bit */
#define BISS_CA_SK_LEN 16            /* AES-128 session key */
#define BISS_CA_SW_LEN 16            /* AES-128 session word (CISSA CW) */
#define BISS_CA_IV_LEN 16            /* AES-CBC IV */
#define BISS_CA_RSA_BYTES 256        /* RSA-2048 block, encrypted_session_data size */
#define BISS_CA_SESSION_DATA_MAX 190 /* RSA-2048-OAEP-SHA256 plaintext capacity */

/* RSA key, pub or priv half. one type: priv load carries pub too, EKID works off either */
typedef struct biss_ca_key biss_ca_key_t;

biss_ca_key_t *biss_ca_key_load_public_file(const char *pem_path);
biss_ca_key_t *biss_ca_key_load_private_file(const char *pem_path);
biss_ca_key_t *biss_ca_key_load_public_mem(const char *pem, size_t len);
biss_ca_key_t *biss_ca_key_load_private_mem(const char *pem, size_t len);
void biss_ca_key_free(biss_ca_key_t *k);

/* leftmost 64 bits of SHA-256(DER SubjectPublicKeyInfo), Tech 3292-s1 SS4.2.1.1. 0 ok, -1 bad args */
int biss_ca_entitlement_key_id(const biss_ca_key_t *k, unsigned char out[BISS_CA_EKID_LEN]);

/* RSA-2048 OAEP, SHA-256 hash + MGF1-SHA256. in_len <= BISS_CA_SESSION_DATA_MAX.
   out always BISS_CA_RSA_BYTES. 0 ok, -1 bad args/no backend/oversized input */
int biss_ca_rsa_encrypt(const biss_ca_key_t *pub, const unsigned char *in, size_t in_len, unsigned char out[BISS_CA_RSA_BYTES]);

/* out_len receives the recovered plaintext length (<= BISS_CA_SESSION_DATA_MAX). 0 ok, -1 fail */
int biss_ca_rsa_decrypt(const biss_ca_key_t *priv, const unsigned char in[BISS_CA_RSA_BYTES], unsigned char *out, size_t out_cap, size_t *out_len);

/* AES-128-CBC, single 16-byte block, no padding. 0 ok, -1 bad args/no backend */
int biss_ca_aes_cbc_encrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char sw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]);
int biss_ca_aes_cbc_decrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char esw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]);

/* CSPRNG bytes, for SW/SK/IV generation. 0 ok, -1 fail */
int biss_ca_random(unsigned char *out, size_t len);

#endif
