/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include <dvbcsa/dvbcsa.h>

#include "csa2.h"

struct csa2_key {
  struct dvbcsa_key_s *k;
};

csa2_key_t *csa2_key_new(const unsigned char cw[CSA2_CW_LEN]) {
  csa2_key_t *k = calloc(1, sizeof *k);
  if (!k)
    return NULL;
  k->k = dvbcsa_key_alloc();
  if (!k->k) {
    free(k);
    return NULL;
  }
  dvbcsa_key_set(cw, k->k);
  return k;
}

void csa2_key_free(csa2_key_t *k) {
  if (!k)
    return;
  dvbcsa_key_free(k->k);
  free(k);
}

void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  dvbcsa_encrypt(k->k, data, (unsigned int)len);
}
