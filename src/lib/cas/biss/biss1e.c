/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>

#include "lib/cas/cw_gen.h"
#include "lib/cas/des.h"
#include "lib/helper/secure_zero.h"

#include "biss.h"

#define BISS1_SW_BITS 48
#define BISS1_SW_MASK ((UINT64_C(1) << BISS1_SW_BITS) - 1)

int biss1_esw_decrypt(const unsigned char id[BISS1_ID_LEN], const unsigned char esw[BISS1_ESW_LEN], int buried, unsigned char cw_out[BISS1_KEY_LEN]) {
  unsigned char key[DES_BLOCK_LEN];
  unsigned char expanded[BISS1_ESW_LEN];
  unsigned char sw[BISS1_SW_LEN];
  des_key_t ks;
  uint64_t v = 0;
  if (!id || !esw || !cw_out)
    return -1;
  des_key_expand(id, key);
  des_set_key(&ks, key);
  des_decrypt(&ks, esw, expanded);
  for (size_t i = 0; i < BISS1_ESW_LEN; i++) v = (v << 6) | ((expanded[i] >> 1) & 0x3F);
  if (buried)
    v = (v >> 1) | ((v & 1) << (BISS1_SW_BITS - 1));
  for (size_t i = 0; i < BISS1_SW_LEN; i++) sw[i] = (unsigned char)(v >> (40 - 8 * i));
  biss1_sw_to_cw(sw, cw_out);
  secure_zero(key, sizeof key);
  secure_zero(expanded, sizeof expanded);
  secure_zero(sw, sizeof sw);
  secure_zero(&ks, sizeof ks);
  return 0;
}

int biss1_esw_encrypt(const unsigned char id[BISS1_ID_LEN], const unsigned char cw[BISS1_KEY_LEN], int buried, unsigned char esw_out[BISS1_ESW_LEN]) {
  unsigned char key[DES_BLOCK_LEN];
  unsigned char expanded[BISS1_ESW_LEN];
  des_key_t ks;
  uint64_t v = 0;
  if (!id || !cw || !esw_out || cw_gen(expanded, BISS1_ESW_LEN, "biss") != 0)
    return -1;
  for (size_t i = 0; i < BISS1_KEY_LEN; i++) {
    if (i != 3 && i != 7)
      v = (v << 8) | cw[i];
  }
  if (buried)
    v = ((v << 1) | (v >> (BISS1_SW_BITS - 1))) & BISS1_SW_MASK;
  for (size_t i = 0; i < BISS1_ESW_LEN; i++) expanded[i] = (unsigned char)((expanded[i] & 0x81) | (((v >> (42 - 6 * i)) & 0x3F) << 1));
  des_key_expand(id, key);
  des_set_key(&ks, key);
  des_encrypt(&ks, expanded, esw_out);
  secure_zero(key, sizeof key);
  secure_zero(expanded, sizeof expanded);
  secure_zero(&ks, sizeof ks);
  return 0;
}
