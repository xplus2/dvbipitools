/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "../../helper/log.h"

#include "biss.h"

int biss_esw_encrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char sw[BISS_KEY_LEN], unsigned char esw_out[BISS_KEY_LEN]) {
  (void)id;
  (void)sw;
  (void)esw_out;
  log_line("biss: this build has no OpenSSL, BISS Mode E (ESW) unavailable");
  return -1;
}

int biss_esw_decrypt(const unsigned char id[BISS_KEY_LEN], const unsigned char esw[BISS_KEY_LEN], unsigned char sw_out[BISS_KEY_LEN]) {
  (void)id;
  (void)esw;
  (void)sw_out;
  log_line("biss: this build has no OpenSSL, BISS Mode E (ESW) unavailable");
  return -1;
}
