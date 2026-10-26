/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/helper/beutil.h"
#include "biss.h"

int biss_parse_hex16(const char *hex, unsigned char out[BISS_KEY_LEN]) {
  if (!hex || !out)
    return -1;
  if ((hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  if (strlen(hex) != BISS_KEY_LEN * 2)
    return -1;
  for (size_t i = 0; i < BISS_KEY_LEN; i++) {
    int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return 0;
}

int biss1_parse_sw(const char *hex, unsigned char cw_out[BISS1_KEY_LEN]) {
  if (!hex || !cw_out)
    return -1;
  if ((hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    hex += 2;
  if (strlen(hex) != BISS1_SW_HEX_LEN)
    return -1;
  for (size_t i = 0; i < 6; i++) {
    int hi = hex_nibble(hex[2 * i]), lo = hex_nibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0)
      return -1;
    cw_out[i < 3 ? i : i + 1] = (unsigned char)((hi << 4) | lo);
  }
  cw_out[3] = (unsigned char)(((unsigned)cw_out[0] + cw_out[1] + cw_out[2]) % 256);
  cw_out[7] = (unsigned char)(((unsigned)cw_out[4] + cw_out[5] + cw_out[6]) % 256);
  return 0;
}
