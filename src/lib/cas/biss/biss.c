/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "biss.h"
#include "evp_block.h"

static int aes128_ecb_block(const unsigned char key[BISS_KEY_LEN], const unsigned char in[BISS_KEY_LEN], unsigned char out[BISS_KEY_LEN], int enc) {
  if (!key || !in || !out)
    return -1;
  return evp_cipher_block(EVP_aes_128_ecb(), key, NULL, in, out, BISS_KEY_LEN, enc);
}

int biss_esw_encrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char sw[BISS_KEY_LEN], unsigned char esw_out[BISS_KEY_LEN]) {
  return aes128_ecb_block(id, sw, esw_out, 1);
}

int biss_esw_decrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char esw[BISS_KEY_LEN], unsigned char sw_out[BISS_KEY_LEN]) {
  return aes128_ecb_block(id, esw, sw_out, 0);
}
