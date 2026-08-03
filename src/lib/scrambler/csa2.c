/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include <dvbcsa/dvbcsa.h>

#include "csa2.h"

struct csa2_key {
  struct dvbcsa_key_s *k;
  struct dvbcsa_bs_key_s *bsk;
};

csa2_key_t *csa2_key_new(const unsigned char cw[CSA2_CW_LEN]) {
  csa2_key_t *k = calloc(1, sizeof *k);
  if (!k)
    return NULL;
  k->k = dvbcsa_key_alloc();
  k->bsk = dvbcsa_bs_key_alloc();
  if (!k->k || !k->bsk) {
    dvbcsa_key_free(k->k);
    dvbcsa_bs_key_free(k->bsk);
    free(k);
    return NULL;
  }
  dvbcsa_key_set(cw, k->k);
  dvbcsa_bs_key_set(cw, k->bsk);
  return k;
}

void csa2_key_free(csa2_key_t *k) {
  if (!k)
    return;
  dvbcsa_key_free(k->k);
  dvbcsa_bs_key_free(k->bsk);
  free(k);
}

void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  dvbcsa_encrypt(k->k, data, (unsigned int)len);
}

void csa2_decrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  dvbcsa_decrypt(k->k, data, (unsigned int)len);
}

unsigned csa2_batch_size(void) {
  return dvbcsa_bs_batch_size();
}

/* maxlen must be a multiple of 8 per libdvbcsa's contract, rounding batch's own longest entry up covers every entry's residue termination
   in one call, verified against the single-packet API on mixed non-8-aligned lengths (real TS payloads vary with adaptation field size) */
static unsigned int csa2_batch_maxlen(const csa2_batch_entry_t *entries, unsigned n) {
  unsigned int maxlen = 0;
  unsigned i;
  for (i = 0; i < n; i++)
    if ((unsigned int)entries[i].len > maxlen)
      maxlen = (unsigned int)entries[i].len;
  return (maxlen + 7u) & ~7u;
}

void csa2_encrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n) {
  struct dvbcsa_bs_batch_s batch[n + 1];
  unsigned i;
  for (i = 0; i < n; i++) {
    batch[i].data = entries[i].data;
    batch[i].len = (unsigned int)entries[i].len;
  }
  batch[n].data = NULL;
  dvbcsa_bs_encrypt(k->bsk, batch, csa2_batch_maxlen(entries, n));
}

void csa2_decrypt_batch(csa2_key_t *k, csa2_batch_entry_t *entries, unsigned n) {
  struct dvbcsa_bs_batch_s batch[n + 1];
  unsigned i;
  for (i = 0; i < n; i++) {
    batch[i].data = entries[i].data;
    batch[i].len = (unsigned int)entries[i].len;
  }
  batch[n].data = NULL;
  dvbcsa_bs_decrypt(k->bsk, batch, csa2_batch_maxlen(entries, n));
}
