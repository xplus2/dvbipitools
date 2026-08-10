/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../../log.h"

#include "biss.h"

static int hexval(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

int biss_parse_hex16(const char *hex, unsigned char out[BISS_KEY_LEN]) {
  size_t i;
  if (!hex || !out)
    return -1;
  if ((hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  if (strlen(hex) != BISS_KEY_LEN * 2)
    return -1;
  for (i = 0; i < BISS_KEY_LEN; i++) {
    int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

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

int biss1_parse_sw(const char *hex, unsigned char cw_out[BISS1_KEY_LEN]) {
  size_t i;
  if (!hex || !cw_out)
    return -1;
  if ((hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  if (strlen(hex) != BISS1_SW_HEX_LEN)
    return -1;
  for (i = 0; i < 6; i++) {
    int hi = hexval(hex[2 * i]), lo = hexval(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    cw_out[i < 3 ? i : i + 1] = (unsigned char)((hi << 4) | lo);
  }
  cw_out[3] = (unsigned char)(((unsigned)cw_out[0] + cw_out[1] + cw_out[2]) % 256);
  cw_out[7] = (unsigned char)(((unsigned)cw_out[4] + cw_out[5] + cw_out[6]) % 256);
  return 0;
}
