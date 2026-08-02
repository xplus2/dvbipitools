/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../log.h"

#include "cissa.h"

/* todo: CISSA isn't _that_ hard (technically and legally).
         once integration testing is done, consider rolling our own, making this stub obsolete */

/* built without OpenSSL: CISSA scrambling always fails, logged once (runs on the per-packet path if ever reached, no log spamming) */
int cissa_encrypt_block(const unsigned char key[CISSA_CW_LEN], unsigned char *data, size_t len) {
  static int warned = 0;
  (void)key;
  (void)data;
  (void)len;
  if (!warned) {
    log_line("cissa: this build has no OpenSSL, CISSA scrambling unavailable");
    warned = 1;
  }
  return -1;
}
