/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../../helper/log.h"

#include "ca.h"

biss_ca_key_t *biss_ca_key_load_public_file(const char *pem_path) {
  (void)pem_path;
  log_line("biss-ca: this build has no OpenSSL, BISS Mode CA unavailable");
  return NULL;
}

biss_ca_key_t *biss_ca_key_load_private_file(const char *pem_path) {
  (void)pem_path;
  log_line("biss-ca: this build has no OpenSSL, BISS Mode CA unavailable");
  return NULL;
}

biss_ca_key_t *biss_ca_key_load_public_mem(const char *pem, size_t len) {
  (void)pem;
  (void)len;
  log_line("biss-ca: this build has no OpenSSL, BISS Mode CA unavailable");
  return NULL;
}

biss_ca_key_t *biss_ca_key_load_private_mem(const char *pem, size_t len) {
  (void)pem;
  (void)len;
  log_line("biss-ca: this build has no OpenSSL, BISS Mode CA unavailable");
  return NULL;
}

void biss_ca_key_free(biss_ca_key_t *k) { (void)k; }

int biss_ca_entitlement_key_id(const biss_ca_key_t *k, unsigned char out[BISS_CA_EKID_LEN]) {
  (void)k;
  (void)out;
  return -1;
}

int biss_ca_rsa_encrypt(const biss_ca_key_t *pub, const unsigned char *in, size_t in_len, unsigned char out[BISS_CA_RSA_BYTES]) {
  (void)pub;
  (void)in;
  (void)in_len;
  (void)out;
  return -1;
}

int biss_ca_rsa_decrypt(const biss_ca_key_t *priv, const unsigned char in[BISS_CA_RSA_BYTES], unsigned char *out, size_t out_cap, size_t *out_len) {
  (void)priv;
  (void)in;
  (void)out;
  (void)out_cap;
  (void)out_len;
  return -1;
}

int biss_ca_aes_cbc_encrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char sw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]) {
  (void)sk;
  (void)iv;
  (void)sw;
  (void)out;
  return -1;
}

int biss_ca_aes_cbc_decrypt(const unsigned char sk[BISS_CA_SK_LEN], const unsigned char iv[BISS_CA_IV_LEN], const unsigned char esw[BISS_CA_SW_LEN], unsigned char out[BISS_CA_SW_LEN]) {
  (void)sk;
  (void)iv;
  (void)esw;
  (void)out;
  return -1;
}

int biss_ca_random(unsigned char *out, size_t len) {
  (void)out;
  (void)len;
  return -1;
}
