/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../log.h"

#include "csa2.h"

/* todo(backlog): libdvbcsa is a quick-start, but we should go AVX-512_BITALG ... some day */

/* built without libdvbcsa: CSA2 scrambling always fails to even acquire a key */
csa2_key_t *csa2_key_new(const unsigned char cw[CSA2_CW_LEN]) {
  (void)cw;
  log_line("csa2: this build has no libdvbcsa, CSA2 scrambling unavailable");
  return NULL;
}

void csa2_key_free(csa2_key_t *k) { (void)k; }

void csa2_encrypt_block(csa2_key_t *k, unsigned char *data, size_t len) {
  (void)k;
  (void)data;
  (void)len;
}
