/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../helper/log.h"

#include "cissa.h"

/* built without OpenSSL: CISSA scrambling always fails to even acquire a key */
cissa_key_t *cissa_key_new(const unsigned char cw[CISSA_CW_LEN]) {
  (void)cw;
  log_line("cissa: this build has no OpenSSL, CISSA scrambling unavailable");
  return NULL;
}

void cissa_key_free(cissa_key_t *k) { (void)k; }

int cissa_encrypt_block(cissa_key_t *k, unsigned char *data, size_t len) {
  (void)k;
  (void)data;
  (void)len;
  return -1;
}

int cissa_decrypt_block(cissa_key_t *k, unsigned char *data, size_t len) {
  (void)k;
  (void)data;
  (void)len;
  return -1;
}
