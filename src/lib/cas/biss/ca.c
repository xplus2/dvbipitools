/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include "ca.h"

struct biss_ca_key {
  EVP_PKEY *pkey;
};

static biss_ca_key_t *wrap_pkey(EVP_PKEY *pkey) {
  biss_ca_key_t *k;
  if (!pkey)
    return NULL;
  k = malloc(sizeof *k);
  if (!k) {
    EVP_PKEY_free(pkey);
    return NULL;
  }
  k->pkey = pkey;
  return k;
}

biss_ca_key_t *biss_ca_key_load_public_file(const char *pem_path) {
  FILE *fp;
  EVP_PKEY *pkey;
  if (!pem_path)
    return NULL;
  fp = fopen(pem_path, "r");
  if (!fp)
    return NULL;
  pkey = PEM_read_PUBKEY(fp, NULL, NULL, NULL);
  fclose(fp);
  return wrap_pkey(pkey);
}

biss_ca_key_t *biss_ca_key_load_private_file(const char *pem_path) {
  FILE *fp;
  EVP_PKEY *pkey;
  if (!pem_path)
    return NULL;
  fp = fopen(pem_path, "r");
  if (!fp)
    return NULL;
  pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
  fclose(fp);
  return wrap_pkey(pkey);
}

biss_ca_key_t *biss_ca_key_load_public_mem(const char *pem, size_t len) {
  BIO *bio;
  EVP_PKEY *pkey;
  if (!pem || len == 0 || len > (size_t)INT_MAX)
    return NULL;
  bio = BIO_new_mem_buf(pem, (int)len);
  if (!bio)
    return NULL;
  pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
  BIO_free(bio);
  return wrap_pkey(pkey);
}

biss_ca_key_t *biss_ca_key_load_private_mem(const char *pem, size_t len) {
  BIO *bio;
  EVP_PKEY *pkey;
  if (!pem || len == 0 || len > (size_t)INT_MAX)
    return NULL;
  bio = BIO_new_mem_buf(pem, (int)len);
  if (!bio)
    return NULL;
  pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
  BIO_free(bio);
  return wrap_pkey(pkey);
}

void biss_ca_key_free(biss_ca_key_t *k) {
  if (!k)
    return;
  EVP_PKEY_free(k->pkey);
  free(k);
}

int biss_ca_entitlement_key_id(const biss_ca_key_t *k, unsigned char out[BISS_CA_EKID_LEN]) {
  unsigned char *der = NULL;
  int der_len;
  unsigned char hash[32];
  unsigned int hash_len;
  int rc = -1;

  if (!k || !k->pkey || !out)
    return -1;

  der_len = i2d_PUBKEY(k->pkey, &der);
  if (der_len <= 0)
    return -1;

  if (EVP_Digest(der, (size_t)der_len, hash, &hash_len, EVP_sha256(), NULL) != 1)
    goto out;
  memcpy(out, hash, BISS_CA_EKID_LEN);
  rc = 0;
out:
  OPENSSL_free(der);
  return rc;
}

int biss_ca_rsa_encrypt(const biss_ca_key_t *pub, const unsigned char *in, size_t in_len, unsigned char out[BISS_CA_RSA_BYTES]) {
  EVP_PKEY_CTX *ctx;
  size_t outlen = BISS_CA_RSA_BYTES;
  int rc = -1;

  if (!pub || !pub->pkey || !in || !out || in_len == 0 || in_len > BISS_CA_SESSION_DATA_MAX)
    return -1;

  ctx = EVP_PKEY_CTX_new(pub->pkey, NULL);
  if (!ctx)
    return -1;

  if (EVP_PKEY_encrypt_init(ctx) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) != 1)
    goto out;
  if (EVP_PKEY_encrypt(ctx, out, &outlen, in, in_len) != 1)
    goto out;
  if (outlen != BISS_CA_RSA_BYTES)
    goto out;

  rc = 0;
out:
  EVP_PKEY_CTX_free(ctx);
  return rc;
}

int biss_ca_rsa_decrypt(const biss_ca_key_t *priv, const unsigned char in[BISS_CA_RSA_BYTES], unsigned char *out, size_t out_cap, size_t *out_len) {
  EVP_PKEY_CTX *ctx;
  /* OpenSSL 3.x providers reject an outlen < RSA modulus size, even for OAEP's shorter plaintext */
  unsigned char scratch[BISS_CA_RSA_BYTES];
  size_t outlen = sizeof scratch;
  int rc = -1;

  if (!priv || !priv->pkey || !in || !out || !out_len)
    return -1;

  ctx = EVP_PKEY_CTX_new(priv->pkey, NULL);
  if (!ctx)
    return -1;

  if (EVP_PKEY_decrypt_init(ctx) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_OAEP_PADDING) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_oaep_md(ctx, EVP_sha256()) != 1)
    goto out;
  if (EVP_PKEY_CTX_set_rsa_mgf1_md(ctx, EVP_sha256()) != 1)
    goto out;
  if (EVP_PKEY_decrypt(ctx, scratch, &outlen, in, BISS_CA_RSA_BYTES) != 1)
    goto out;
  if (outlen > out_cap)
    goto out;
  memcpy(out, scratch, outlen);

  *out_len = outlen;
  rc = 0;
out:
  EVP_PKEY_CTX_free(ctx);
  return rc;
}

static int aes128_cbc_block(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char in[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN], int enc) {
  EVP_CIPHER_CTX *ctx;
  int outlen, totlen;
  int rc = -1;

  ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return -1;

  if (enc ? EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, sk, iv) != 1
          : EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, sk, iv) != 1)
    goto out;
  EVP_CIPHER_CTX_set_padding(ctx, 0);

  if (enc ? EVP_EncryptUpdate(ctx, out, &outlen, in, BISS_CA_SW_LEN) != 1
          : EVP_DecryptUpdate(ctx, out, &outlen, in, BISS_CA_SW_LEN) != 1)
    goto out;
  if (outlen != BISS_CA_SW_LEN)
    goto out;
  totlen = outlen;
  if (enc ? EVP_EncryptFinal_ex(ctx, out + totlen, &outlen) != 1
          : EVP_DecryptFinal_ex(ctx, out + totlen, &outlen) != 1)
    goto out;

  rc = 0;
out:
  EVP_CIPHER_CTX_free(ctx);
  return rc;
}

int biss_ca_aes_cbc_encrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char sw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]) {
  if (!sk || !iv || !sw || !out)
    return -1;
  return aes128_cbc_block(sk, iv, sw, out, 1);
}

int biss_ca_aes_cbc_decrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char esw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]) {
  if (!sk || !iv || !esw || !out)
    return -1;
  return aes128_cbc_block(sk, iv, esw, out, 0);
}

int biss_ca_random(unsigned char *out, size_t len) {
  if (!out || len == 0 || len > (size_t)INT_MAX)
    return -1;
  return RAND_bytes(out, (int)len) == 1 ? 0 : -1;
}
